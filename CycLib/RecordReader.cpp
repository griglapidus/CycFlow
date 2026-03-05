// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#define NOMINMAX
#include "RecordReader.h"
#include "Core/CycLogger.h"
#include <algorithm>

namespace cyc {

RecordReader::RecordReader(std::shared_ptr<RecBuffer> target, size_t batchCapacity)
    : m_target(target)
    , m_rule(target->getRule())
    , m_recSize(target->getRecSize())
    , m_capacity(batchCapacity)
    , m_activeIdx(0)
    , m_activeCount(0)
    , m_bgCount(0)
    , m_bgIsFull(false)
    , m_running(true)
    , m_finishing(false)
    , m_finishTarget(0)
{
    // Initialize the reader cursor. If the buffer already has data,
    // we start reading only the newly incoming data (skipping the old history).
    auto totalAndSize = m_target->getTotalWrittenAndSize();
    uint64_t totalWritten = std::get<0>(totalAndSize);
    size_t currentBufferSize = std::get<1>(totalAndSize);

    if (totalWritten > currentBufferSize) {
        m_readerCursor.store(totalWritten - currentBufferSize, std::memory_order_relaxed);
    } else {
        m_readerCursor.store(0, std::memory_order_relaxed);
    }

    m_target->addClient(this);

    // Allocate memory for double buffering
    m_bufferA.resize(m_capacity * m_recSize);
    m_bufferB.resize(m_capacity * m_recSize);
    m_activeBuf = &m_bufferA;
    m_bgBuf = &m_bufferB;

    LOG_INFO << "RecordReader created: batchCapacity=" << m_capacity
             << " recSize=" << m_recSize
             << " bufferMemory=" << (m_capacity * m_recSize * 2) << "B"
             << " targetBufferCapacity=" << m_target->capacity()
             << " initialCursor=" << m_readerCursor.load()
             << " totalWritten=" << totalWritten
             << " currentBufferSize=" << currentBufferSize;

    m_worker = std::thread(&RecordReader::workerLoop, this);
}

RecordReader::~RecordReader() {
    stop();
    m_target->removeClient(this);
}

void RecordReader::notifyDataAvailable() {
    m_cv_worker.notify_one();
}

uint64_t RecordReader::getCursor() const {
    return m_readerCursor.load(std::memory_order_acquire);
}

void RecordReader::stop() {
    bool expected = true;
    if (m_running.compare_exchange_strong(expected, false)) {
        LOG_INFO << "RecordReader::stop: stopping reader"
                 << " cursor=" << m_readerCursor.load();
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

    m_finishing = true;
    m_finishTarget = m_target->getTotalWritten();

    LOG_INFO << "RecordReader::finish: finishing at target=" << m_finishTarget
             << " currentCursor=" << m_readerCursor.load()
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
        return false; // Background worker hasn't finished preparing the next batch
    }

    std::swap(m_activeBuf, m_bgBuf);
    m_activeCount = m_bgCount;
    m_activeIdx = 0;

    LOG_DBG << "RecordReader::swapBuffers: swapped, activeCount=" << m_activeCount;

    // Reset background state. Note: No memset is performed here for maximum throughput.
    m_bgCount = 0;
    m_bgIsFull = false;

    lock.unlock();
    m_cv_worker.notify_one(); // Wake up worker to fetch more data
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
    size_t count = std::min(maxRecords, available);

    const uint8_t* ptr = m_activeBuf->data() + (m_activeIdx * m_recSize);
    m_activeIdx += count;

    return {ptr, count, m_rule, m_recSize};
}

Record RecordReader::nextRecord() {
    auto batch = nextBatch(1, true);
    if (batch.isValid()) {
        // Safe cast: we only provide read-only views to the user,
        // but the internal Record structure holds a non-const void* pointer.
        return Record(m_rule, const_cast<uint8_t*>(batch.data));
    }
    return Record(m_rule, nullptr);
}

void RecordReader::workerLoop() {
    LOG_DBG << "RecordReader::workerLoop: background thread started"
            << " capacity=" << m_capacity
            << " targetCapacity=" << m_target->capacity();

    while (m_running.load()) {
        size_t countToRead = 0;

        {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_cv_worker.wait(lock, [this]() {
                if (m_bgIsFull) return false; // Must wait for the user to consume the batch

                if (!m_running.load()) return true;

                if (m_finishing && m_readerCursor.load() >= m_finishTarget) return true;

                uint64_t diff = m_target->getTotalWritten() - m_readerCursor.load();
                return diff > 0; // Data is available
            });

            if (!m_running.load()) return;

            if (m_finishing && m_readerCursor.load() >= m_finishTarget) {
                LOG_DBG << "RecordReader::workerLoop: finish target reached"
                        << " cursor=" << m_readerCursor.load()
                        << " finishTarget=" << m_finishTarget;
                m_running.store(false);
                m_cv_user.notify_all();
                return;
            }
        }

        auto totalAndSize = m_target->getTotalWrittenAndSize();
        uint64_t totalWritten = std::get<0>(totalAndSize);
        size_t currentBufferSize = std::get<1>(totalAndSize);

        uint64_t cursor = m_readerCursor.load(std::memory_order_relaxed);
        uint64_t lag = totalWritten - cursor;

        if (lag == 0) continue;

        // If the reader is too slow and data was overwritten in the circular buffer,
        // skip to the oldest available valid data to recover gracefully.
        if (lag > currentBufferSize) {
            uint64_t oldCursor = cursor;
            cursor = totalWritten - currentBufferSize;
            lag = currentBufferSize;

            LOG_WARN << "RecordReader::workerLoop: reader lagging behind, data was overwritten"
                     << " skippedRecords=" << (cursor - oldCursor)
                     << " oldCursor=" << oldCursor
                     << " newCursor=" << cursor
                     << " bufferSize=" << currentBufferSize;
        }

        countToRead = std::min(static_cast<size_t>(lag), m_capacity);

        LOG_TRACE << "RecordReader::workerLoop: reading chunk"
                  << " cursor=" << cursor
                  << " countToRead=" << countToRead
                  << " lag=" << lag
                  << " totalWritten=" << totalWritten
                  << " bufferSize=" << currentBufferSize;

        // Fetch data directly into the background buffer
        size_t actuallyRead = m_target->readFromGlobal(cursor, m_bgBuf->data(), countToRead);

        if (actuallyRead == 0 && countToRead > 0) {
            LOG_TRACE << "RecordReader::workerLoop: readFromGlobal returned 0"
                      << " (cursor possibly invalidated), retrying";
            continue; // Cursor might have been concurrently invalidated, retry next loop
        }

        m_readerCursor.store(cursor + actuallyRead, std::memory_order_release);

        if (countToRead > 0) {
            m_target->notifyWriters();
        }

        // Mark background buffer as ready
        if (actuallyRead > 0) {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_bgCount = actuallyRead;
            m_bgIsFull = true;

            LOG_DBG << "RecordReader::workerLoop: bg buffer ready"
                    << " actuallyRead=" << actuallyRead
                    << " (" << (actuallyRead * m_recSize) << "B)"
                    << " newCursor=" << m_readerCursor.load();
        }

        m_cv_user.notify_one();
    }

    LOG_DBG << "RecordReader::workerLoop: background thread exiting";
}

} // namespace cyc
