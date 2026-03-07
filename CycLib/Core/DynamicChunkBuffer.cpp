// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "DynamicChunkBuffer.h"
#include "CircularBuffer.h"
#include <atomic>
#include <cassert>

namespace cyc {

// ---------------------------------------------------------------------------
// DynamicChunkBuffer::Impl — hidden implementation
// ---------------------------------------------------------------------------
struct DynamicChunkBuffer::Impl {
    // Reordered to minimize internal padding bytes
    alignas(64) std::atomic<uint64_t> totalWritten;
    size_t                            chunkSize;
    cyc::CircularBuffer<uint8_t>      buffer;

    Impl(size_t itemCapacity, size_t _chunkSize)
        : totalWritten(0)
        , chunkSize(_chunkSize)
        , buffer(itemCapacity * _chunkSize)
    {
        assert(itemCapacity > 0);
        assert(_chunkSize > 0);
    }
};

// ---------------------------------------------------------------------------
DynamicChunkBuffer::DynamicChunkBuffer(size_t itemCapacity, size_t chunkSize)
    : m_impl(std::make_unique<Impl>(itemCapacity, chunkSize))
{}

DynamicChunkBuffer::~DynamicChunkBuffer() = default;

DynamicChunkBuffer::DynamicChunkBuffer(DynamicChunkBuffer&&) noexcept = default;
DynamicChunkBuffer& DynamicChunkBuffer::operator=(DynamicChunkBuffer&&) noexcept = default;

// ---------------------------------------------------------------------------
void DynamicChunkBuffer::push(const void* data, size_t count) {
    if (!data || count == 0) return;
    m_impl->buffer.push_many(static_cast<const uint8_t*>(data), count * m_impl->chunkSize);
    m_impl->totalWritten.fetch_add(count, std::memory_order_release);
}

void DynamicChunkBuffer::readAt(size_t index, void* destination, size_t count) const {
    if (!destination || count == 0) return;
    m_impl->buffer.peek_many_at(index * m_impl->chunkSize,
                                static_cast<uint8_t*>(destination),
                                count * m_impl->chunkSize);
}

std::pair<const uint8_t*, bool> DynamicChunkBuffer::getChunkPtr(size_t index) const {
    size_t byteIndex = index * m_impl->chunkSize;
    const uint8_t* ptr = m_impl->buffer.get_ptr_unsafe(byteIndex);

    size_t capacityBytes = m_impl->buffer.capacity();
    size_t head = m_impl->buffer.get_head_index_unsafe();
    size_t physicalStart = (head + byteIndex) % capacityBytes;

    bool isSplit = (physicalStart + m_impl->chunkSize) > capacityBytes;
    return {ptr, isSplit};
}

uint64_t DynamicChunkBuffer::getTotalWritten() const {
    return m_impl->totalWritten.load(std::memory_order_acquire);
}

size_t DynamicChunkBuffer::size() const {
    return m_impl->buffer.size() / m_impl->chunkSize;
}

size_t DynamicChunkBuffer::capacity() const {
    return m_impl->buffer.capacity() / m_impl->chunkSize;
}

size_t DynamicChunkBuffer::getChunkSize() const {
    return m_impl->chunkSize;
}

void DynamicChunkBuffer::clear() {
    m_impl->buffer.clear();
}

} // namespace cyc
