// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "RecordReader.h"

namespace cyc {

RecordReader::RecordReader(std::shared_ptr<RecBuffer> target, size_t batchCapacity)
    : m_target(target)
    , m_rule(target->getRule())       // Get rule from RecBuffer
    , m_recSize(target->getRecSize()) // Get size from RecBuffer
    , m_capacity(batchCapacity)
    , m_activeIdx(0)
    , m_activeCount(0)
    , m_bgCount(0)
    , m_running(true)
    , m_bgIsFull(false)
    , m_finishing(false)      // Init
    , m_finishTarget(0)       // Init
{
    auto totalAndSize = m_target->getTotalWrittenAndSize();
    uint64_t totalWritten = std::get<0>(totalAndSize);
    size_t currentBufferSize = std::get<1>(totalAndSize);
    m_readerCursor = totalWritten - currentBufferSize;
    m_target->addClient(this);

    m_bufferA.resize(m_capacity * m_recSize);
    m_bufferB.resize(m_capacity * m_recSize);
    m_activeBuf = &m_bufferA;
    m_bgBuf = &m_bufferB;

    m_worker = std::thread(&RecordReader::workerLoop, this);
}

RecordReader::~RecordReader() {
    m_target->removeClient(this);
    stop();
}

Record RecordReader::nextRecord() {
    if (m_activeIdx >= m_activeCount) {
        if (!swapBuffers()) {
            return Record(m_rule, nullptr);
        }
    }
    uint8_t* ptr = m_activeBuf->data() + (m_activeIdx * m_recSize);
    m_activeIdx++;
    return Record(m_rule, ptr);
}

RecordReader::RecordBatch RecordReader::nextBatch(size_t maxCount, bool wait)
{
    // 1. Проверяем, есть ли данные в текущем активном буфере
    size_t availableInActive = m_activeCount - m_activeIdx;

    if (availableInActive > 0) {
        size_t countToReturn = std::min(availableInActive, maxCount);
        uint8_t* ptr = m_activeBuf->data() + (m_activeIdx * m_recSize);
        m_activeIdx += countToReturn;
        return {ptr, countToReturn, m_rule, m_recSize};
    }

    // 2. Активный буфер пуст. Пытаемся поменять буферы.
    // Передаем параметр wait: если false, swapBuffers вернет false при отсутствии данных.
    if (!swapBuffers(wait)) {
        // Данных нет (или остановка)
        return {nullptr, 0, m_rule, m_recSize};
    }

    // 3. Свап прошел успешно, данные появились
    size_t available = m_activeCount; // m_activeIdx сброшен в 0 внутри swapBuffers
    size_t countToReturn = std::min(available, maxCount);
    uint8_t* ptr = m_activeBuf->data();
    m_activeIdx = countToReturn;

    return {ptr, countToReturn, m_rule, m_recSize};
}

void RecordReader::notifyDataAvailable() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_cv_worker.notify_one();
}

void RecordReader::stop() {
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_running = false;
    }
    m_cv_worker.notify_all();
    m_cv_user.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void RecordReader::finish() {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (!m_running) return;

    m_finishTarget = m_target->getTotalWritten();
    m_finishing = true;

    m_cv_worker.notify_all();
}

bool RecordReader::hasData() const
{
    if (m_activeIdx < m_activeCount) return true;
    return m_bgIsFull;
}

bool RecordReader::swapBuffers(bool wait) {
    std::unique_lock<std::mutex> lock(m_mtx);

    if (wait) {
        while (m_bgCount == 0 && m_running) {
            m_cv_user.wait(lock);
        }
    } else {
        if (m_bgCount == 0) {
            return false;
        }
    }

    if (!m_running && !m_bgIsFull) return false;

    std::swap(m_activeBuf, m_bgBuf);
    m_activeCount = m_bgCount;
    m_activeIdx = 0;
    m_bgIsFull = false;
    m_bgCount = 0;

    lock.unlock();
    m_cv_worker.notify_one();
    return true;
}

void RecordReader::workerLoop() {
    while (true) {
        size_t countToRead = 0;

        {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_cv_worker.wait(lock, [this] {
                if (!m_running) return true;
                if (m_bgIsFull) return false;

                if (m_finishing && m_readerCursor >= m_finishTarget) return true;

                uint64_t diff = m_target->getTotalWritten() - m_readerCursor;
                return diff > 0;
            });

            if (!m_running) return;

            if (m_finishing && m_readerCursor >= m_finishTarget) {
                m_running = false;
                m_cv_user.notify_all();
                return;
            }
        }

        auto totalAndSize = m_target->getTotalWrittenAndSize();
        uint64_t totalWritten = std::get<0>(totalAndSize);
        size_t currentBufferSize = std::get<1>(totalAndSize);

        uint64_t lag = totalWritten - m_readerCursor;

        if (lag == 0) continue;

        if (lag > currentBufferSize) {
            m_readerCursor = totalWritten - currentBufferSize;
            lag = currentBufferSize;
        }

        countToRead = std::min(static_cast<size_t>(lag), m_capacity);

        size_t actuallyRead = m_target->readFromGlobal(m_readerCursor, m_bgBuf->data(), countToRead);

        if (actuallyRead == 0 && countToRead > 0) {
            continue;
        }

        m_readerCursor += countToRead;

        if (countToRead > 0) {
            m_target->notifyWriters();
        }

        if (countToRead > 0) {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_bgCount = countToRead;
            m_bgIsFull = true;
            m_cv_user.notify_one();
        }
    }
}



} // namespace cyc
