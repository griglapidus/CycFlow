// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "RecordReader.h"

namespace cyc {

AsyncRecordReader::AsyncRecordReader(std::shared_ptr<RecBuffer> target, size_t batchCapacity)
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
    m_target->addReaderForNotification(this);
    m_readerCursor = m_target->getTotalWritten();

    m_bufferA.resize(m_capacity * m_recSize);
    m_bufferB.resize(m_capacity * m_recSize);
    m_activeBuf = &m_bufferA;
    m_bgBuf = &m_bufferB;

    m_worker = std::thread(&AsyncRecordReader::workerLoop, this);
}

AsyncRecordReader::~AsyncRecordReader() {
    m_target->removeReaderForNotification(this);
    stop();
}

Record AsyncRecordReader::nextRecord() {
    if (m_activeIdx >= m_activeCount) {
        if (!swapBuffers()) {
            return Record(m_rule, nullptr);
        }
    }
    uint8_t* ptr = m_activeBuf->data() + (m_activeIdx * m_recSize);
    m_activeIdx++;
    return Record(m_rule, ptr);
}

void AsyncRecordReader::notifyDataAvailable() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_cv_worker.notify_one();
}

void AsyncRecordReader::stop() {
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

void AsyncRecordReader::finish() {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (!m_running) return;

    m_finishTarget = m_target->getTotalWritten();
    m_finishing = true;

    m_cv_worker.notify_all();
}

bool AsyncRecordReader::swapBuffers() {
    std::unique_lock<std::mutex> lock(m_mtx);
    while (m_bgCount == 0 && m_running) {
        m_cv_user.wait(lock);
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

void AsyncRecordReader::workerLoop() {
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

        uint64_t totalWritten = m_target->getTotalWritten();
        size_t currentBufferSize = m_target->size();

        uint64_t lag = totalWritten - m_readerCursor;

        if (lag == 0) continue;

        if (lag > currentBufferSize) {
            m_readerCursor = totalWritten - currentBufferSize;
            lag = currentBufferSize;
        }

        countToRead = std::min(static_cast<size_t>(lag), m_capacity);

        size_t relativeIndex = currentBufferSize - lag;
        m_target->readRelative(relativeIndex, m_bgBuf->data(), countToRead);
        m_readerCursor += countToRead;

        if (countToRead > 0) {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_bgCount = countToRead;
            m_bgIsFull = true;
            m_cv_user.notify_one();
        }
    }
}



} // namespace cyc
