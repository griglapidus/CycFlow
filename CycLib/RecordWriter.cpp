// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "RecordWriter.h"
#include "Core/PReg.h"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace cyc {

RecordWriter::RecordWriter(std::shared_ptr<RecBuffer> target, size_t batchCapacity, bool blockOnFull)
    : m_target(target)
    , m_rule(m_target->getRule())
    , m_recSize(m_target->getRecSize())
    , m_capacity(batchCapacity)
    , m_earlyThreshold(std::max<size_t>(1, batchCapacity / 5))
    , m_blockOnFull(blockOnFull)
    , m_currentIdx(0)
    , m_bgCount(0)
    , m_running(true)
    , m_hasWork(false)
{
    if (m_blockOnFull) {
        m_target->addWriter(this);
    }

    m_bufferA.resize(m_capacity * m_recSize);
    m_bufferB.resize(m_capacity * m_recSize);
    m_activeBuf = &m_bufferA;
    m_bgBuf = &m_bufferB;

    m_timestampId = PReg::getID("TimeStamp");

    m_worker = std::thread(&RecordWriter::workerLoop, this);
}

RecordWriter::~RecordWriter() {
    stop();
    // removeWriter вызывается в stop() или здесь, но лучше гарантированно:
    if (m_blockOnFull) {
        m_target->removeWriter(this);
    }
}

// --- Single Record API ---

Record RecordWriter::nextRecord() {
    // Если буфер полон или почти полон (early threshold)
    if (m_currentIdx >= m_capacity) {
        swapBuffers(true); // Блокирующий свап, если места совсем нет
    } else if (m_currentIdx >= m_earlyThreshold) {
        swapBuffers(false); // Попытка неблокирующего свапа
    }

    uint8_t* ptr = m_activeBuf->data() + (m_currentIdx * m_recSize);
    return Record(m_rule, ptr);
}

void RecordWriter::commitRecord() {
    uint8_t* ptr = m_activeBuf->data() + (m_currentIdx * m_recSize);
    Record rec(m_rule, ptr);

    if (rec.getDouble(m_timestampId) == 0.0) {
        rec.setDouble(m_timestampId, get_current_epoch_time());
    }

    m_currentIdx++;
}

RecordWriter::RecordBatch RecordWriter::nextBatch() {
    if (m_currentIdx >= m_capacity) {
        swapBuffers(true);
    } else if (m_currentIdx >= m_earlyThreshold) {
        swapBuffers(false);
    }

    uint8_t* ptr = m_activeBuf->data() + (m_currentIdx * m_recSize);
    size_t availableCount = m_capacity - m_currentIdx;

    return {ptr, availableCount, m_rule, m_recSize};
}

void RecordWriter::commitBatch(size_t count) {
    if (count == 0) return;

    size_t available = m_capacity - m_currentIdx;
    if (count > available) count = available;

    for (size_t i = 0; i < count; ++i) {
        uint8_t* ptr = m_activeBuf->data() + ((m_currentIdx + i) * m_recSize);
        Record rec(m_rule, ptr);
        if (rec.getDouble(m_timestampId) == 0.0) {
            rec.setDouble(m_timestampId, get_current_epoch_time());
        }
    }

    // Сдвигаем курсор после обработки
    m_currentIdx += count;
}

// --- Common Logic ---

void RecordWriter::flush() {
    if (m_currentIdx > 0) {
        swapBuffers(true);
    }
}

void RecordWriter::stop() {
    // Сначала флашим остатки
    flush();

    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_running = false;
        m_hasWork = true; // Будим worker, чтобы он увидел !m_running
    }
    m_cv.notify_one();

    if (m_worker.joinable()) {
        m_worker.join();
    }
}

bool RecordWriter::swapBuffers(bool blocking) {
    std::unique_lock<std::mutex> lock(m_mtx, std::defer_lock);

    if (blocking) {
        lock.lock();
        // Ждем, пока worker освободит bgBuf
        while (m_hasWork && m_running) {
            m_cv_done.wait(lock);
        }
    } else {
        if (!lock.try_lock()) return false;
        if (m_hasWork) return false;
    }

    // В этот момент bgBuf свободен (worker его сбросил или еще не брал)

    // Передаем размер текущего (заполненного) буфера в bgCount
    m_bgCount = m_currentIdx;

    // Меняем указатели
    std::swap(m_activeBuf, m_bgBuf);

    // Сбрасываем текущий (теперь это бывший bgBuf, который стал active)
    // std::memset не обязателен, если мы всегда пишем поверх,
    // но полезен для дебага и безопасности (чтобы не читать мусор).
    // Для высокой производительности memset можно убрать.
    std::memset(m_activeBuf->data(), 0, m_activeBuf->size());

    m_currentIdx = 0;
    m_hasWork = true; // Сигнализируем worker'у

    lock.unlock();
    m_cv.notify_one();
    return true;
}

void RecordWriter::workerLoop() {
    while (true) {
        size_t countToFlush = 0;
        std::vector<uint8_t>* bufToFlush = nullptr;

        {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_cv.wait(lock, [this] { return m_hasWork; });

            // Если остановили и данных нет -> выход
            if (!m_running && m_bgCount == 0) return;

            bufToFlush = m_bgBuf;
            countToFlush = m_bgCount;
        }

        if (countToFlush > 0) {
            if (!m_blockOnFull) {
                // Неблокирующая запись (если буфер переполнен, старые данные могут перезаписаться в RecBuffer)
                m_target->push(bufToFlush->data(), countToFlush);
            } else {
                // Блокирующая запись (ждем место в RecBuffer)
                size_t writtenSoFar = 0;
                const uint8_t* dataPtr = bufToFlush->data();

                while (writtenSoFar < countToFlush) {
                    size_t available = m_target->getAvailableWriteSpace();

                    if (available == 0) {
                        // Ждем место
                        m_target->waitForSpace([this]() {
                            return !m_running;
                        });
                        if (!m_running) break;
                        continue;
                    }

                    size_t remaining = countToFlush - writtenSoFar;
                    size_t chunk = std::min(remaining, available);

                    m_target->push(dataPtr + (writtenSoFar * m_recSize), chunk);
                    writtenSoFar += chunk;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_hasWork = false;
            m_bgCount = 0;
        }
        // Уведомляем user thread, что swapBuffers может продолжать
        m_cv_done.notify_one();
    }
}

} // namespace cyc
