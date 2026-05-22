// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#define NOMINMAX
#include "RecordWriterZC.h"
#include "Core/CycLogger.h"
#include <algorithm>
#include <cstring>

namespace cyc {

RecordWriterZC::RecordWriterZC() = default;

RecordWriterZC::RecordWriterZC(std::shared_ptr<RecBuffer> target, size_t batchCapacity, bool blockOnFull) {
    init(target, batchCapacity, blockOnFull);
}

void RecordWriterZC::init(std::shared_ptr<RecBuffer> target, size_t batchCapacity, bool blockOnFull) {
    initBase(target, batchCapacity, blockOnFull);
    m_buf.resize(m_capacity * m_recSize);

    LOG_INFO << "RecordWriterZC created: bufferMemory=" << m_buf.size() << "B";
}

// --- helpers -----------------------------------------------------------------

void RecordWriterZC::waitForSpace(size_t needed) {
    if (!m_blockOnFull) return;
    while (m_target->getAvailableWriteSpace() < needed) {
        m_target->waitForSpace([this]() { return false; });
    }
}

// --- Single-record API -------------------------------------------------------

Record RecordWriterZC::nextRecord() {
    uint8_t* ptr = m_buf.data();
    std::memset(ptr, 0, m_recSize);
    return Record(m_rule, ptr);
}

void RecordWriterZC::commitRecord() {
    uint8_t* ptr = m_buf.data();

    double& ts = *reinterpret_cast<double*>(ptr + m_timestampOffset);
    if (ts == 0.0) {
        ts = get_current_epoch_time();
    }

    waitForSpace(1);
    m_target->push(ptr, 1);
}

// --- Batch API ---------------------------------------------------------------

RecordWriterZC::RecordBatch RecordWriterZC::nextBatch(size_t maxRecords, bool /*wait*/) {
    m_pendingCount = std::min(maxRecords, m_capacity);
    std::memset(m_buf.data(), 0, m_pendingCount * m_recSize);
    return {m_buf.data(), m_pendingCount, m_rule, m_recSize};
}

void RecordWriterZC::commitBatch(size_t count) {
    if (count == 0) return;
    count = std::min(count, m_pendingCount);

    for (size_t i = 0; i < count; ++i) {
        double& ts = *reinterpret_cast<double*>(m_buf.data() + i * m_recSize + m_timestampOffset);
        if (ts == 0.0) {
            ts = get_current_epoch_time();
        }
    }

    if (m_blockOnFull) {
        size_t pushed = 0;
        while (pushed < count) {
            size_t avail = m_target->getAvailableWriteSpace();
            if (avail == 0) {
                m_target->waitForSpace([this]() { return false; });
                continue;
            }
            size_t chunk = std::min(count - pushed, avail);
            m_target->push(m_buf.data() + pushed * m_recSize, chunk);
            pushed += chunk;
        }
    } else {
        m_target->push(m_buf.data(), count);
    }

    m_pendingCount = 0;

    LOG_DBG << "RecordWriterZC::commitBatch: pushed " << count
            << " records (" << (count * m_recSize) << "B)";
}

} // namespace cyc
