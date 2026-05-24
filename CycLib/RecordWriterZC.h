// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDWRITERZC_H
#define CYC_RECORDWRITERZC_H

#include "RecordWriterBase.h"

namespace cyc {
CYCLIB_SUPPRESS_C4251

/**
 * @class RecordWriterZC
 * @brief Synchronous, single-buffered writer for RecBuffer.
 *
 * Eliminates the double-buffer copy and background worker thread present in
 * RecordWriter. Each commitRecord() / commitBatch() call pushes data directly
 * to the target RecBuffer in the caller's thread.
 *
 * ### Memory model
 * A single heap buffer of @c batchCapacity records is allocated at init time.
 * nextRecord() and nextBatch() return pointers into this buffer. On commit the
 * data is forwarded to RecBuffer in one push() call — one memory copy total,
 * compared to two in the double-buffered RecordWriter.
 *
 * ### Backpressure
 * When @c blockOnFull is @c true and the target buffer has no space, commit
 * calls block the caller until readers free capacity. The caller's thread
 * therefore stalls instead of a separate worker thread.
 *
 * ### When to use
 * Prefer this class when:
 * - The producer can tolerate occasional stalls (batch sizes are small or
 *   the ring buffer is sized generously relative to reader throughput).
 * - Deterministic latency is more important than throughput isolation.
 * - Thread and memory overhead of RecordWriter are undesirable.
 *
 * For high-throughput producers that must never stall see RecordWriter.
 *
 * @warning **Single-producer only.** RecordWriterZC must be the sole writer
 * attached to its target RecBuffer. If more than one producer needs to feed
 * the same buffer, every writer must be a RecordWriter — mixing writer types
 * or using multiple RecordWriterZC instances on one buffer is unsupported.
 *
 * @see RecordWriter
 */
class CYCLIB_EXPORT RecordWriterZC : public RecordWriterBase {
public:
    /// @brief Type alias — mirrors RecordWriter::RecordBatch for uniform producer code.
    using RecordBatch = RecordWriterBase::RecordBatch;

    /**
     * @brief Default constructor. Leaves the writer uninitialised.
     * Call init() before use.
     */
    RecordWriterZC();

    /**
     * @brief Constructs and initialises the writer.
     * @param target       Shared pointer to the destination RecBuffer.
     * @param batchCapacity  Maximum records the internal buffer can hold.
     * @param blockOnFull  If @c true, commit calls stall when the target is full.
     */
    RecordWriterZC(std::shared_ptr<RecBuffer> target, size_t batchCapacity, bool blockOnFull = true);

    ~RecordWriterZC() override = default;

    /**
     * @brief Initialises the writer. Must be called once on default-constructed instances.
     * @param target       Shared pointer to the destination RecBuffer.
     * @param batchCapacity  Maximum records the internal buffer can hold.
     * @param blockOnFull  If @c true, commit calls stall when the target is full.
     */
    void init(std::shared_ptr<RecBuffer> target, size_t batchCapacity, bool blockOnFull = true);

    // -------------------------------------------------------------------------
    // RecordWriterBase overrides
    // -------------------------------------------------------------------------

    /**
     * @brief Returns a zeroed record slot in the internal buffer.
     * @copydoc RecordWriterBase::nextRecord
     */
    Record nextRecord() override;

    /**
     * @brief Timestamps the record and pushes it directly to RecBuffer.
     * @copydoc RecordWriterBase::commitRecord
     */
    void commitRecord() override;

    /**
     * @brief Returns up to @p maxRecords zeroed slots from the internal buffer.
     * @copydoc RecordWriterBase::nextBatch
     */
    RecordBatch nextBatch(size_t maxRecords, bool wait = true) override;

    /**
     * @brief Timestamps @p count records and pushes them directly to RecBuffer.
     * @copydoc RecordWriterBase::commitBatch
     */
    void commitBatch(size_t count) override;

    /**
     * @brief No-op — RecordWriterZC pushes synchronously; there is no deferred data.
     */
    void flush() override {}

    /**
     * @brief Unblocks any thread currently waiting in commitBatch() / commitRecord().
     *
     * Sets an internal stop flag so that a pending waitForSpace() returns
     * immediately even if the target buffer is still full. After stop() any
     * subsequent commit call that would block is silently skipped.
     *
     * Call this before joining the producer thread to avoid a deadlock when
     * the ring buffer is full and readers have stopped advancing their cursor.
     */
    void stop();

private:
    void waitForSpace(size_t needed);

private:
    std::vector<uint8_t>  m_buf;               ///< Single write buffer (no double-buffering).
    size_t                m_pendingCount = 0;  ///< Records reserved by the last nextBatch() call.
    std::atomic<bool>     m_running{true};     ///< Set to false by stop() to unblock waitForSpace().
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_RECORDWRITERZC_H
