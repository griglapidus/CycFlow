// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "RecordReader.h"
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
        m_cv_worker.notify_all();
        m_cv_user.notify_all();
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }
}

void RecordReader::finish() {
    if (!m_running.load()) return;

    m_finishing = true;
    m_finishTarget = m_target->getTotalWritten();
    m_cv_worker.notify_all();

    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_running.store(false);
}

bool RecordReader::swapBuffers() {
    std::unique_lock<std::mutex> lock(m_mtx);
    if (!m_bgIsFull && m_running.load()) {
        return false; // Background worker hasn't finished preparing the next batch
    }

    std::swap(m_activeBuf, m_bgBuf);
    m_activeCount = m_bgCount;
    m_activeIdx = 0;

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
            cursor = totalWritten - currentBufferSize;
            lag = currentBufferSize;
        }

        countToRead = std::min(static_cast<size_t>(lag), m_capacity);

        // Fetch data directly into the background buffer
        size_t actuallyRead = m_target->readFromGlobal(cursor, m_bgBuf->data(), countToRead);

        if (actuallyRead == 0 && countToRead > 0) {
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
        }

        m_cv_user.notify_one();
    }
}

} // namespace cyc
