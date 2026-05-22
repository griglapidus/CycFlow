// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#define NOMINMAX
#include "RecordWriterBase.h"
#include "Core/PReg.h"
#include "Core/CycLogger.h"

namespace cyc {

const RecRule& RecordWriterBase::getRule() const {
    return m_rule;
}

void RecordWriterBase::initBase(std::shared_ptr<RecBuffer> target, size_t batchCapacity, bool blockOnFull) {
    m_target       = target;
    m_rule         = target->getRule();
    m_recSize      = target->getRecSize();
    m_capacity     = batchCapacity;
    m_blockOnFull  = blockOnFull;
    m_timestampId  = PReg::getID("TimeStamp");
    m_timestampOffset = m_rule.getOffsetById(m_timestampId);

    LOG_INFO << "RecordWriterBase::initBase:"
             << " batchCapacity=" << m_capacity
             << " recSize=" << m_recSize
             << " blockOnFull=" << (m_blockOnFull ? "true" : "false")
             << " targetCapacity=" << m_target->capacity();
}

} // namespace cyc
