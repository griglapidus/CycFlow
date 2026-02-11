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
    {
        std::unique_lock<std::shared_mutex> lock(m_dataRwMtx);
        m_impl.push(data, count);
    }
    notifyReaders();
}

void RecBuffer::readRelative(size_t index, void *dest, size_t count) const {
    std::shared_lock<std::shared_mutex> lock(m_dataRwMtx);
    m_impl.readAt(index, dest, count);
}

const RecRule &RecBuffer::getRule() const { return m_rule; }

size_t RecBuffer::getRecSize() const { return m_impl.getChunkSize(); }

size_t RecBuffer::size() const {
    std::shared_lock<std::shared_mutex> lock(m_dataRwMtx);
    return m_impl.size();
}

size_t RecBuffer::capacity() const {
    std::shared_lock<std::shared_mutex> lock(m_dataRwMtx);
    return m_impl.capacity();
}

uint64_t RecBuffer::getTotalWritten() const {
    std::shared_lock<std::shared_mutex> lock(m_dataRwMtx);
    return m_impl.getTotalWritten();
}

void RecBuffer::addReaderForNotification(RecordReader *reader)
{
    std::lock_guard<std::mutex> lock(m_syncMtx);
    auto readerIt = std::find(m_readers.begin(), m_readers.end(), reader);
    if(readerIt == m_readers.end()) {
        m_readers.push_back(reader);
    }
}

void RecBuffer::removeReaderForNotification(RecordReader *reader)
{
    std::lock_guard<std::mutex> lock(m_syncMtx);
    auto readerIt = std::find(m_readers.begin(), m_readers.end(), reader);
    if(readerIt != m_readers.end()) {
        m_readers.erase(readerIt);
    }
    notifyWriters();
}

void RecBuffer::addWriter(RecordWriter *writer)
{
    std::lock_guard<std::mutex> lock(m_syncMtx);
    auto it = std::find(m_writers.begin(), m_writers.end(), writer);
    if(it == m_writers.end()) {
        m_writers.push_back(writer);
    }
}

void RecBuffer::removeWriter(RecordWriter *writer)
{
    std::lock_guard<std::mutex> lock(m_syncMtx);
    auto it = std::find(m_writers.begin(), m_writers.end(), writer);
    if(it != m_writers.end()) {
        m_writers.erase(it);
    }
}

size_t RecBuffer::getAvailableWriteSpace() const
{
    std::lock_guard<std::mutex> lock(m_syncMtx);
    return getAvailableWriteSpace_nolock();
}

void RecBuffer::waitForSpace(std::function<bool ()> stopCondition)
{
    std::unique_lock<std::mutex> lock(m_syncMtx);
    m_spaceCv.wait(lock, [this, &stopCondition]() {
        return getAvailableWriteSpace_nolock() > 0 || stopCondition();
    });
}

void RecBuffer::notifyReaders() const
{
    std::lock_guard<std::mutex> lock(m_syncMtx);
    for(auto *reader:m_readers) {
        reader->notifyDataAvailable();
    }
}

size_t RecBuffer::getAvailableWriteSpace_nolock() const
{
    if (m_readers.empty()) {
        return capacity();
    }

    uint64_t totalWritten = m_impl.getTotalWritten();
    uint64_t minReaderCursor = totalWritten;

    for (const auto* reader : m_readers) {
        uint64_t cursor = reader->getCursor();
        if (cursor < minReaderCursor) {
            minReaderCursor = cursor;
        }
    }

    uint64_t unreadCount = totalWritten - minReaderCursor;
    if (unreadCount >= capacity()) {
        return 0;
    }

    return capacity() - static_cast<size_t>(unreadCount);
}

void RecBuffer::notifyWriters()
{
    m_spaceCv.notify_all();
}

} // namespace cyc
