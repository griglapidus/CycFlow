// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "RecBuffer.h"
#include <cassert>
#include <algorithm>

namespace cyc {

RecBuffer::RecBuffer(const RecRule &rule, size_t capacity)
    : m_rule(rule)
    , m_impl(capacity, rule.getRecSize())
    , m_phantomReadCursor(0)
{
}

void RecBuffer::push(const void *data, size_t count) {
    {
        std::unique_lock<std::shared_mutex> lock(m_dataRwMtx);
        m_impl.push(data, count);
    }
    notifyClients();
}

void RecBuffer::readRelative(size_t index, void *dest, size_t count) const {
    std::shared_lock<std::shared_mutex> lock(m_dataRwMtx);
    m_impl.readAt(index, dest, count);
}

void RecBuffer::processRecord(size_t index, std::function<void (const Record &)> visitor) const {
    std::shared_lock<std::shared_mutex> lock(m_dataRwMtx);

    if (index >= m_impl.size()) {
        Record emptyRec(m_rule, nullptr);
        visitor(emptyRec);
        return;
    }

    auto [ptr, isSplit] = m_impl.getChunkPtr(index);
    // Architecture guarantees records never straddle the buffer boundary
    assert(!isSplit && "Memory wrap-around within a single chunk is impossible by design");

    Record rec(m_rule, const_cast<uint8_t*>(ptr));
    visitor(rec);
}

bool RecBuffer::copyRecord(size_t index, Record& dest) const {
    if (!dest.isValid() || dest.getSize() != getRecSize()) {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(m_dataRwMtx);

    if (index >= m_impl.size()) {
        return false;
    }

    m_impl.readAt(index, dest.data(), 1);
    return true;
}

const RecRule& RecBuffer::getRule() const { return m_rule; }
size_t RecBuffer::getRecSize() const { return m_impl.getChunkSize(); }

size_t RecBuffer::size() const {
    std::shared_lock<std::shared_mutex> lock(m_dataRwMtx);
    return m_impl.size();
}

size_t RecBuffer::capacity() const {
    return m_impl.capacity();
}

uint64_t RecBuffer::getTotalWritten() const {
    return m_impl.getTotalWritten();
}

std::tuple<uint64_t, size_t> RecBuffer::getTotalWrittenAndSize() const {
    std::shared_lock<std::shared_mutex> lock(m_dataRwMtx);
    return {m_impl.getTotalWritten(), m_impl.size()};
}

void RecBuffer::addClient(IRecBufferClient* client) {
    std::lock_guard<std::mutex> lock(m_syncMtx);
    if (std::find(m_clients.begin(), m_clients.end(), client) == m_clients.end()) {
        m_clients.push_back(client);
    }
}

void RecBuffer::removeClient(IRecBufferClient* client) {
    std::lock_guard<std::mutex> lock(m_syncMtx);
    auto it = std::find(m_clients.begin(), m_clients.end(), client);
    if (it != m_clients.end()) {
        m_clients.erase(it);
    }
    notifyWriters();
}

size_t RecBuffer::getAvailableWriteSpace() const {
    std::lock_guard<std::mutex> lock(m_syncMtx);
    return getAvailableWriteSpace_nolock();
}

void RecBuffer::waitForSpace(const std::function<bool()>& stopCondition) {
    std::unique_lock<std::mutex> lock(m_syncMtx);
    m_spaceCv.wait(lock, [this, &stopCondition]() {
        return getAvailableWriteSpace_nolock() > 0 || stopCondition();
    });
}

void RecBuffer::notifyClients() const {
    std::lock_guard<std::mutex> lock(m_syncMtx);
    for (auto* client : m_clients) {
        client->notifyDataAvailable();
    }
}

size_t RecBuffer::getAvailableWriteSpace_nolock() const {
    uint64_t totalWritten = m_impl.getTotalWritten();
    size_t cap = m_impl.capacity();
    uint64_t minReaderCursor = calculateMinReadCursor_nolock();

    if (totalWritten < minReaderCursor) return cap;

    uint64_t unreadCount = totalWritten - minReaderCursor;
    return (unreadCount >= cap) ? 0 : cap - static_cast<size_t>(unreadCount);
}

uint64_t RecBuffer::calculateMinReadCursor_nolock() const {
    if (m_clients.empty()) return m_phantomReadCursor;

    uint64_t minCursor = UINT64_MAX;
    for (const auto* client : m_clients) {
        uint64_t c = client->getCursor();
        if (c != UINT64_MAX && c < minCursor) {
            minCursor = c;
        }
    }

    if (minCursor == UINT64_MAX) return m_phantomReadCursor;

    m_phantomReadCursor = minCursor;
    return minCursor;
}

void RecBuffer::notifyWriters() {
    m_spaceCv.notify_all();
}

size_t RecBuffer::readFromGlobal(uint64_t globalCursor, void *dest, size_t count) const {
    std::shared_lock<std::shared_mutex> lock(m_dataRwMtx);

    uint64_t totalWritten = m_impl.getTotalWritten();
    size_t currentSize = m_impl.size();
    uint64_t lag = totalWritten - globalCursor;

    if (lag > currentSize || lag == 0) return 0;

    size_t relativeIndex = currentSize - lag;
    m_impl.readAt(relativeIndex, dest, count);
    return count;
}

} // namespace cyc
