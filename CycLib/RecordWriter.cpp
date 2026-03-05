// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#define NOMINMAX
#include "RecordWriter.h"
#include "Core/PReg.h"
#include "Core/CycLogger.h"
#include <algorithm>
#include <cstring>

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
    // Allocate memory for the double buffers
    m_bufferA.resize(m_capacity * m_recSize);
    m_bufferB.resize(m_capacity * m_recSize);
    m_activeBuf = &m_bufferA;
    m_bgBuf = &m_bufferB;

    m_timestampId = PReg::getID("TimeStamp");

    LOG_INFO << "RecordWriter created: batchCapacity=" << m_capacity
             << " recSize=" << m_recSize
             << " bufferMemory=" << (m_capacity * m_recSize * 2) << "B"
             << " blockOnFull=" << (m_blockOnFull ? "true" : "false")
             << " targetBufferCapacity=" << m_target->capacity();

    // Start the background flushing thread
    m_worker = std::thread(&RecordWriter::workerLoop, this);
}

RecordWriter::~RecordWriter() {
    stop();
}

void RecordWriter::stop() {
    flush(); // Ensure all remaining data is saved before stopping
    m_running.store(false, std::memory_order_release);
    m_cv.notify_all();

    if (m_worker.joinable()) {
        m_worker.join();
    }

    LOG_INFO << "RecordWriter stopped";
}

// --- Single Record API ---

Record RecordWriter::nextRecord() {
    if (m_currentIdx >= m_capacity) {
        LOG_TRACE << "RecordWriter::nextRecord: active buffer full (" << m_capacity
                  << " records), swapping buffers";
        swapBuffers(true); // Block until background buffer is flushed
    }

    uint8_t* ptr = m_activeBuf->data() + (m_currentIdx * m_recSize);
    std::memset(ptr, 0, m_recSize);
    return Record(m_rule, ptr);
}

void RecordWriter::commitRecord() {
    if (m_currentIdx < m_capacity) {
        uint8_t* ptr = m_activeBuf->data() + (m_currentIdx * m_recSize);
        Record rec(m_rule, ptr);

        if (rec.getDouble(m_timestampId) == 0.0) {
            rec.setDouble(m_timestampId, get_current_epoch_time());
        }
        ++m_currentIdx;
    }
}

// --- Batch API ---

RecordWriter::RecordBatch RecordWriter::nextBatch(size_t maxRecords, bool wait) {
    if (m_currentIdx >= m_capacity) {
        LOG_DBG << "RecordWriter::nextBatch: active buffer full, swapping (wait=" << wait << ")";
        if (!swapBuffers(wait)) {
            LOG_WARN << "RecordWriter::nextBatch: swap failed (worker busy, non-blocking)";
            return {nullptr, 0, m_rule, m_recSize}; // Failed to swap (e.g., worker busy and wait=false)
        }
    }

    size_t available = m_capacity - m_currentIdx;
    size_t count = std::min(maxRecords, available);

    uint8_t* ptr = m_activeBuf->data() + (m_currentIdx * m_recSize);
    std::memset(ptr, 0, count * m_recSize);

    LOG_DBG << "RecordWriter::nextBatch: providing " << count
            << " record slots (requested=" << maxRecords
            << " available=" << available << ")";

    return {ptr, count, m_rule, m_recSize};
}

void RecordWriter::commitBatch(size_t count) {
    if (m_currentIdx + count <= m_capacity) {
        size_t available = m_capacity - m_currentIdx;
        if (count > available) count = available;

        for (size_t i = 0; i < count; ++i) {
            uint8_t* ptr = m_activeBuf->data() + ((m_currentIdx + i) * m_recSize);
            Record rec(m_rule, ptr);
            if (rec.getDouble(m_timestampId) == 0.0) {
                rec.setDouble(m_timestampId, get_current_epoch_time());
            }
        }
        m_currentIdx += count;

        LOG_DBG << "RecordWriter::commitBatch: committed " << count
                << " records, bufferFill=" << m_currentIdx << "/" << m_capacity;
    } else {
        // Safety fallback in case of incorrect count provided by the user
        LOG_WARN << "RecordWriter::commitBatch: count " << count
                 << " exceeds remaining capacity (idx=" << m_currentIdx
                 << " cap=" << m_capacity << "), clamping to full";
        m_currentIdx = m_capacity;
    }
}

// --- Control API ---

bool RecordWriter::swapBuffers(bool blocking) {
    std::unique_lock<std::mutex> lock(m_mtx);

    if (blocking) {
        m_cv_done.wait(lock, [this]() { return !m_hasWork || !m_running.load(std::memory_order_acquire); });
    } else if (m_hasWork) {
        // Background thread is still busy processing the previous buffer
        return false;
    }

    if (!m_running.load(std::memory_order_acquire)) {
        return false;
    }

    // Perform the pointer swap
    std::swap(m_activeBuf, m_bgBuf);
    m_bgCount = m_currentIdx;

    LOG_DBG << "RecordWriter::swapBuffers: swapped, bgCount=" << m_bgCount
            << " (blocking=" << blocking << ")";

    // Reset active index.
    m_currentIdx = 0;

    m_hasWork = true;
    lock.unlock();

    // Wake up the background worker
    m_cv.notify_one();

    return true;
}

void RecordWriter::flush() {
    if (m_currentIdx > 0) {
        LOG_DBG << "RecordWriter::flush: flushing " << m_currentIdx << " pending records";
        swapBuffers(true);
    }

    // Wait until the background worker finishes flushing the newly swapped buffer
    std::unique_lock<std::mutex> lock(m_mtx);
    m_cv_done.wait(lock, [this] {
        return !m_hasWork || !m_running;
    });
}

// --- Worker Thread ---

void RecordWriter::workerLoop() {
    LOG_DBG << "RecordWriter::workerLoop: background thread started";

    while (m_running.load(std::memory_order_acquire)) {
        std::vector<uint8_t>* bufToFlush = nullptr;
        size_t countToFlush = 0;

        {
            std::unique_lock<std::mutex> lock(m_mtx);

            // Wait until there is work to do or we are requested to stop
            m_cv.wait(lock, [this]() { return m_hasWork || !m_running.load(std::memory_order_acquire); });

            if (!m_running.load(std::memory_order_acquire) && !m_hasWork) {
                break;
            }

            bufToFlush = m_bgBuf;
            countToFlush = m_bgCount;
        }

        if (countToFlush > 0) {
            LOG_DBG << "RecordWriter::workerLoop: flushing chunk of " << countToFlush
                    << " records (" << (countToFlush * m_recSize) << "B)"
                    << " to target buffer (capacity=" << m_target->capacity() << ")";

            if (!m_blockOnFull) {
                // Non-blocking write: push directly to RecBuffer.
                m_target->push(bufToFlush->data(), countToFlush);

                LOG_TRACE << "RecordWriter::workerLoop: non-blocking push complete"
                          << " count=" << countToFlush;
            } else {
                // Blocking write: wait for readers to free up space
                size_t writtenSoFar = 0;
                const uint8_t* dataPtr = bufToFlush->data();

                while (writtenSoFar < countToFlush) {
                    size_t available = m_target->getAvailableWriteSpace();

                    if (available == 0) {
                        LOG_TRACE << "RecordWriter::workerLoop: target buffer full, waiting for space"
                                  << " (written " << writtenSoFar << "/" << countToFlush << ")";

                        m_target->waitForSpace([this]() {
                            return !m_running.load(std::memory_order_acquire);
                        });

                        if (!m_running.load(std::memory_order_acquire)) {
                            break;
                        }
                        continue;
                    }

                    size_t remaining = countToFlush - writtenSoFar;
                    size_t chunk = std::min(remaining, available);

                    m_target->push(dataPtr + (writtenSoFar * m_recSize), chunk);
                    writtenSoFar += chunk;

                    LOG_TRACE << "RecordWriter::workerLoop: blocking push chunk=" << chunk
                              << " writtenSoFar=" << writtenSoFar << "/" << countToFlush
                              << " availableSpace=" << available;
                }
            }
        }

        // Cleanup after flush is completed
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_hasWork = false;
            m_bgCount = 0;
        }

        // Notify flush() or swapBuffers(true) that the background buffer is free
        m_cv_done.notify_all();
    }

    LOG_DBG << "RecordWriter::workerLoop: background thread exiting";
}

} // namespace cyc
