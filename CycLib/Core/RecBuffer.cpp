// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "RecBuffer.h"
#include <assert.h>

namespace cyc {

RecBuffer::RecBuffer(const RecRule &rule, size_t capacity)
        : m_rule(rule)
        , m_impl(capacity, rule.getRecSize()) // Pass only size to impl
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

void RecBuffer::processRecord(size_t index, std::function<void (const Record &)> visitor) const
{
    std::shared_lock<std::shared_mutex> lock(m_dataRwMtx);

    if (index >= m_impl.size()) {
        Record emptyRec(m_rule, nullptr);
        visitor(emptyRec);
        return;
    }

    auto [ptr, isSplit] = m_impl.getChunkPtr(index);

    if (!isSplit) {
        Record rec(m_rule, const_cast<uint8_t*>(ptr));
        visitor(rec);
    } else {
        std::vector<uint8_t> tempBuf(m_rule.getRecSize());
        m_impl.readAt(index, tempBuf.data(), 1);

        Record rec(m_rule, tempBuf.data());
        visitor(rec);
    }
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

std::tuple<uint64_t, size_t> RecBuffer::getTotalWrittenAndSize() const
{
    std::shared_lock<std::shared_mutex> lock(m_dataRwMtx);
    return std::make_tuple(m_impl.getTotalWritten(), m_impl.size());
}

void RecBuffer::addClient(IRecBufferClient* client)
{
    std::lock_guard<std::mutex> lock(m_syncMtx);
    auto it = std::find(m_clients.begin(), m_clients.end(), client);
    if (it == m_clients.end()) {
        m_clients.push_back(client);
    }
}

void RecBuffer::removeClient(IRecBufferClient* client)
{
    std::lock_guard<std::mutex> lock(m_syncMtx);
    auto it = std::find(m_clients.begin(), m_clients.end(), client);
    if (it != m_clients.end()) {
        m_clients.erase(it);
    }
    notifyWriters();
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

void RecBuffer::notifyClients() const
{
    std::lock_guard<std::mutex> lock(m_syncMtx);
    for(auto *client:m_clients) {
        client->notifyDataAvailable();
    }
}

size_t RecBuffer::getAvailableWriteSpace_nolock() const
{
    uint64_t totalWritten;
    size_t cap;
    {
        std::shared_lock<std::shared_mutex> lock(m_dataRwMtx);
        totalWritten = m_impl.getTotalWritten();
        cap = m_impl.capacity();
    }

    uint64_t minReaderCursor = calculateMinReadCursor_nolock();

    if (totalWritten < minReaderCursor) return cap;

    uint64_t unreadCount = totalWritten - minReaderCursor;

    if (unreadCount >= cap) {
        return 0;
    }

    return cap - static_cast<size_t>(unreadCount);
}

uint64_t RecBuffer::calculateMinReadCursor_nolock() const
{
    if (m_clients.empty()) {
        return m_phantomReadCursor;
    }

    uint64_t minCursor = UINT64_MAX;
    for (const auto* client : m_clients) {
        uint64_t c = client->getCursor();
        if (c != UINT64_MAX && c < minCursor) {
            minCursor = c;
        }
    }

    if (minCursor == UINT64_MAX) {
        return m_phantomReadCursor;
    }

    m_phantomReadCursor = minCursor;
    return minCursor;
}

void RecBuffer::notifyWriters()
{
    m_spaceCv.notify_all();
}

size_t RecBuffer::readFromGlobal(uint64_t globalCursor, void *dest, size_t count) const
{
    std::shared_lock<std::shared_mutex> lock(m_dataRwMtx);

    uint64_t totalWritten = m_impl.getTotalWritten();
    size_t currentSize = m_impl.size();

    uint64_t lag = totalWritten - globalCursor;

    // Проверка: если данные уже перезаписаны (lag > size), читать нельзя
    if (lag > currentSize) {
        return 0;
    }

    // Если читать нечего
    if (lag == 0) {
        return 0;
    }

    // Безопасный расчет индекса внутри лока
    size_t relativeIndex = currentSize - lag;
    m_impl.readAt(relativeIndex, dest, count);
    return count;
}

} // namespace cyc
