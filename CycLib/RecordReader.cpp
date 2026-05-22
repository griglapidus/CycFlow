// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#define NOMINMAX
#include "RecordReader.h"
#include "Core/CycLogger.h"
#include <algorithm>

namespace cyc {

RecordReader::RecordReader() = default;

RecordReader::RecordReader(std::shared_ptr<RecBuffer> target, size_t batchCapacity) {
    init(target, batchCapacity);
}

void RecordReader::init(std::shared_ptr<RecBuffer> target, size_t batchCapacity) {
    initBase(target, batchCapacity);

    m_bufferA.resize(m_capacity * m_recSize);
    m_bufferB.resize(m_capacity * m_recSize);
    m_activeBuf = &m_bufferA;
    m_bgBuf     = &m_bufferB;

    LOG_INFO << "RecordReader created: bufferMemory=" << (m_capacity * m_recSize * 2) << "B";

    m_worker = std::thread(&RecordReader::workerLoop, this);
}

RecordReader::~RecordReader() {
    stop();
    if (m_target) {
        m_target->removeClient(this);
    }
}

void RecordReader::notifyDataAvailable() {
    {
        std::lock_guard<std::mutex> lock(m_mtx);
    }
    m_cv_worker.notify_one();
}

void RecordReader::stop() {
    bool expected = true;
    if (m_running.compare_exchange_strong(expected, false)) {
        LOG_INFO << "RecordReader::stop: cursor=" << m_readerCursor.load();
        {
            std::lock_guard<std::mutex> lock(m_mtx);
        }
        m_cv_worker.notify_all();
        m_cv_user.notify_all();
        if (m_worker.joinable()) {
            m_worker.join();
        }
        LOG_INFO << "RecordReader stopped";
    }
}

void RecordReader::finish() {
    if (!m_running.load()) return;

    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_finishing    = true;
        m_finishTarget = m_target->getTotalWritten();
    }

    LOG_INFO << "RecordReader::finish: target=" << m_finishTarget
             << " cursor=" << m_readerCursor.load()
             << " remaining=" << (m_finishTarget - m_readerCursor.load());

    m_cv_worker.notify_all();

    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_running.store(false);
    LOG_INFO << "RecordReader finished";
}

bool RecordReader::swapBuffers() {
    std::unique_lock<std::mutex> lock(m_mtx);
    if (!m_bgIsFull && m_running.load()) {
        return false;
    }

    std::swap(m_activeBuf, m_bgBuf);
    m_activeCount = m_bgCount;
    m_activeIdx   = 0;

    LOG_DBG << "RecordReader::swapBuffers: activeCount=" << m_activeCount;

    m_bgCount  = 0;
    m_bgIsFull = false;

    lock.unlock();
    m_cv_worker.notify_one();
    return true;
}

RecordReader::RecordBatch RecordReader::nextBatch(size_t maxRecords, bool wait) {
    if (m_activeIdx >= m_activeCount) {
        std::unique_lock<std::mutex> lock(m_mtx);

        if (wait) {
            m_cv_user.wait(lock, [this]() {
                return m_bgIsFull || !m_running.load();
            });
        }

        lock.unlock();

        if (!swapBuffers()) {
            return {nullptr, 0, m_rule, m_recSize};
        }
    }

    if (m_activeIdx >= m_activeCount) {
        return {nullptr, 0, m_rule, m_recSize};
    }

    size_t available = m_activeCount - m_activeIdx;
    size_t count     = std::min(maxRecords, available);

    const uint8_t* ptr = m_activeBuf->data() + (m_activeIdx * m_recSize);
    m_activeIdx += count;

    return {ptr, count, m_rule, m_recSize};
}

void RecordReader::workerLoop() {
    LOG_DBG << "RecordReader::workerLoop: started"
            << " capacity=" << m_capacity
            << " targetCapacity=" << m_target->capacity();

    while (m_running.load()) {
        size_t countToRead = 0;

        {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_cv_worker.wait(lock, [this]() {
                if (!m_running.load()) return true;
                if (m_bgIsFull)        return false;
                if (m_finishing && m_readerCursor.load() >= m_finishTarget) return true;
                return (m_target->getTotalWritten() - m_readerCursor.load()) > 0;
            });

            if (!m_running.load()) return;

            if (m_finishing && m_readerCursor.load() >= m_finishTarget) {
                LOG_DBG << "RecordReader::workerLoop: finish target reached"
                        << " cursor=" << m_readerCursor.load();
                m_running.store(false);
                m_cv_user.notify_all();
                return;
            }
        }

        auto [totalWritten, currentBufferSize] = m_target->getTotalWrittenAndSize();
        uint64_t cursor = m_readerCursor.load(std::memory_order_relaxed);
        uint64_t lag    = totalWritten - cursor;

        if (lag == 0) continue;

        if (lag > currentBufferSize) {
            uint64_t oldCursor = cursor;
            cursor = totalWritten - currentBufferSize;
            lag    = currentBufferSize;

            LOG_WARN << "RecordReader::workerLoop: lagging, skipped "
                     << (cursor - oldCursor) << " records"
                     << " oldCursor=" << oldCursor
                     << " newCursor=" << cursor;
        }

        countToRead = std::min(static_cast<size_t>(lag), m_capacity);

        LOG_TRACE << "RecordReader::workerLoop: reading"
                  << " cursor=" << cursor
                  << " count=" << countToRead
                  << " lag=" << lag;

        size_t actuallyRead = m_target->readFromGlobal(cursor, m_bgBuf->data(), countToRead);

        if (actuallyRead == 0 && countToRead > 0) {
            LOG_TRACE << "RecordReader::workerLoop: readFromGlobal=0, retrying";
            continue;
        }

        m_readerCursor.store(cursor + actuallyRead, std::memory_order_release);

        if (countToRead > 0) {
            m_target->notifyWriters();
        }

        if (actuallyRead > 0) {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_bgCount  = actuallyRead;
            m_bgIsFull = true;

            LOG_DBG << "RecordReader::workerLoop: bg ready"
                    << " actuallyRead=" << actuallyRead
                    << " (" << (actuallyRead * m_recSize) << "B)"
                    << " cursor=" << m_readerCursor.load();
        }

        m_cv_user.notify_one();
    }

    LOG_DBG << "RecordReader::workerLoop: exiting";
}

} // namespace cyc
