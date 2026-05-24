// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#define NOMINMAX
#include "RecordReaderZC.h"
#include "Core/CycLogger.h"
#include <algorithm>

namespace cyc {

RecordReaderZC::RecordReaderZC() = default;

RecordReaderZC::RecordReaderZC(std::shared_ptr<RecBuffer> target, size_t batchCapacity) {
    init(target, batchCapacity);
}

void RecordReaderZC::init(std::shared_ptr<RecBuffer> target, size_t batchCapacity) {
    initBase(target, batchCapacity);
    m_pinnedEnd = m_readerCursor.load(std::memory_order_relaxed);
    LOG_INFO << "RecordReaderZC created";
}

RecordReaderZC::~RecordReaderZC() {
    stop();
    if (m_target) {
        m_target->removeClient(this);
    }
}

void RecordReaderZC::notifyDataAvailable() {
    // Acquire/release m_mtx to synchronise with a thread that is between
    // the predicate check and entering the wait state inside cv.wait().
    // Without this barrier the notify may be lost on stricter cv
    // implementations (MSVC/SRWLock) and the reader will hang.
    { std::lock_guard<std::mutex> lock(m_mtx); }
    m_cv.notify_one();
}

void RecordReaderZC::stop() {
    bool expected = true;
    if (m_running.compare_exchange_strong(expected, false)) {
        { std::lock_guard<std::mutex> lock(m_mtx); }
        m_cv.notify_all();
        LOG_INFO << "RecordReaderZC stopped: cursor=" << m_readerCursor.load();
    }
}

void RecordReaderZC::finish() {
    m_finishTarget.store(m_target->getTotalWritten(), std::memory_order_relaxed);
    m_finishing.store(true, std::memory_order_release);
    { std::lock_guard<std::mutex> lock(m_mtx); }
    m_cv.notify_all();

    LOG_INFO << "RecordReaderZC::finish: target=" << m_finishTarget.load()
             << " cursor=" << m_readerCursor.load();
}

void RecordReaderZC::release() {
    uint64_t pinned  = m_pinnedEnd;
    uint64_t current = m_readerCursor.load(std::memory_order_relaxed);
    if (pinned == current) return;

    m_readerCursor.store(pinned, std::memory_order_release);
    if (m_target) {
        m_target->notifyWriters();
    }
}

RecordReaderZC::RecordBatch RecordReaderZC::nextBatch(size_t maxRecords, bool wait) {
    release(); // free the previous pinned batch

    uint64_t cursor = m_readerCursor.load(std::memory_order_relaxed);
    maxRecords = std::min(maxRecords, m_capacity);

    bool     finishing = m_finishing.load(std::memory_order_acquire);
    uint64_t finishT   = finishing ? m_finishTarget.load(std::memory_order_relaxed) : 0;

    if (finishing && cursor >= finishT) {
        m_running.store(false, std::memory_order_release);
        return {nullptr, 0, m_rule, m_recSize};
    }
    if (!m_running.load(std::memory_order_acquire)) {
        return {nullptr, 0, m_rule, m_recSize};
    }

    if (wait) {
        std::unique_lock<std::mutex> lock(m_mtx);
        m_cv.wait(lock, [&]() {
            if (!m_running.load(std::memory_order_acquire)) return true;
            bool fin    = m_finishing.load(std::memory_order_acquire);
            uint64_t ft = fin ? m_finishTarget.load(std::memory_order_relaxed) : 0;
            if (fin && cursor >= ft) return true;
            return m_target->getTotalWritten() > cursor;
        });
    }

    finishing = m_finishing.load(std::memory_order_acquire);
    finishT   = finishing ? m_finishTarget.load(std::memory_order_relaxed) : 0;

    if (!m_running.load(std::memory_order_acquire)) {
        return {nullptr, 0, m_rule, m_recSize};
    }
    if (finishing && cursor >= finishT) {
        m_running.store(false, std::memory_order_release);
        return {nullptr, 0, m_rule, m_recSize};
    }

    size_t         contiguous = 0;
    const uint8_t* ptr = m_target->getBatchPtrFromGlobal(cursor, maxRecords, contiguous);

    if (!ptr || contiguous == 0) {
        // Check for overrun: writer (blockOnFull=false) may have advanced past our cursor
        // by more than the buffer capacity, making the cursor stale.
        auto [tw, bs] = m_target->getTotalWrittenAndSize();
        if (bs > 0 && tw > cursor && (tw - cursor) > bs) {
            const uint64_t newCursor = tw - bs;
            m_readerCursor.store(newCursor, std::memory_order_release);
            // Keep the invariant pinnedEnd == readerCursor (nothing is pinned yet) so
            // that the next release() call does not revert the cursor back to the
            // stale pinnedEnd value.
            m_pinnedEnd = newCursor;
            m_target->notifyWriters();
            LOG_WARN << "RecordReaderZC::nextBatch: overrun, skipped "
                     << (newCursor - cursor) << " records to cursor=" << newCursor;
            ptr = m_target->getBatchPtrFromGlobal(newCursor, maxRecords, contiguous);
            cursor = newCursor;
        }
        if (!ptr || contiguous == 0) {
            return {nullptr, 0, m_rule, m_recSize};
        }
    }

    m_pinnedEnd = cursor + contiguous;

    LOG_DBG << "RecordReaderZC::nextBatch: cursor=" << cursor
            << " contiguous=" << contiguous
            << " (" << (contiguous * m_recSize) << "B)";

    return {ptr, contiguous, m_rule, m_recSize};
}

} // namespace cyc
