// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#define NOMINMAX
#include "RecordReaderBase.h"
#include "Core/CycLogger.h"

namespace cyc {

uint64_t RecordReaderBase::getCursor() const {
    return m_readerCursor.load(std::memory_order_acquire);
}

const RecRule& RecordReaderBase::getRule() const {
    return m_rule;
}

Record RecordReaderBase::nextRecord() {
    auto batch = nextBatch(1, true);
    if (batch.isValid()) {
        // Safe const_cast: Record API requires non-const ptr but we expose read-only semantics.
        return Record(m_rule, const_cast<uint8_t*>(batch.data));
    }
    return Record(m_rule, nullptr);
}

void RecordReaderBase::initBase(std::shared_ptr<RecBuffer> target, size_t batchCapacity) {
    m_target   = target;
    m_rule     = target->getRule();
    m_recSize  = target->getRecSize();
    m_capacity = batchCapacity;
    m_running.store(true, std::memory_order_release);

    auto [totalWritten, currentBufferSize] = m_target->getTotalWrittenAndSize();
    uint64_t startCursor = (totalWritten > currentBufferSize)
        ? totalWritten - currentBufferSize
        : 0;
    m_readerCursor.store(startCursor, std::memory_order_relaxed);

    m_target->addClient(this);

    LOG_INFO << "RecordReaderBase::initBase:"
             << " batchCapacity=" << m_capacity
             << " recSize=" << m_recSize
             << " targetCapacity=" << m_target->capacity()
             << " initialCursor=" << startCursor
             << " totalWritten=" << totalWritten
             << " currentBufferSize=" << currentBufferSize;
}

} // namespace cyc
