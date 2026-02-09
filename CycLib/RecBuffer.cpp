// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "RecBuffer.h"
#include <assert.h>
#include "RecordReader.h"

namespace cyc {

RecBuffer::RecBuffer(const RecRule &rule, size_t capacity)
        : m_rule(rule)
        , m_impl(capacity, rule.getRecSize()) // Pass only size to impl
    {
    }

void RecBuffer::push(const void *data, size_t count) {
    m_impl.push(data, count);
    notifyReaders();
}

void RecBuffer::readRelative(size_t index, void *dest, size_t count) const {
    m_impl.readAt(index, dest, count);
}

const RecRule &RecBuffer::getRule() const { return m_rule; }

size_t RecBuffer::getRecSize() const { return m_impl.getChunkSize(); }

size_t RecBuffer::size() const { return m_impl.size(); }

size_t RecBuffer::capacity() const { return m_impl.capacity(); }

uint64_t RecBuffer::getTotalWritten() const { return m_impl.getTotalWritten(); }

void RecBuffer::addReaderForNotification(AsyncRecordReader *reader)
{
    std::lock_guard<std::mutex> lock(m_readersMtx);
    auto readerIt = std::find(m_readers.begin(), m_readers.end(), reader);
    if(readerIt == m_readers.end()) {
        m_readers.push_back(reader);
    }
}

void RecBuffer::removeReaderForNotification(AsyncRecordReader *reader)
{
    std::lock_guard<std::mutex> lock(m_readersMtx);
    auto readerIt = std::find(m_readers.begin(), m_readers.end(), reader);
    if(readerIt != m_readers.end()) {
        m_readers.erase(readerIt);
    }
}

void RecBuffer::notifyReaders()
{
    std::lock_guard<std::mutex> lock(m_readersMtx);
    for(auto *reader:m_readers) {
        reader->notifyDataAvailable();
    }
}

} // namespace cyc
