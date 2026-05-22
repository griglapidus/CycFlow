// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDREADERZC_H
#define CYC_RECORDREADERZC_H

#include "RecordReaderBase.h"
#include <condition_variable>

namespace cyc {
CYCLIB_SUPPRESS_C4251

/**
 * @class RecordReaderZC
 * @brief Zero-copy, synchronous reader for RecBuffer.
 *
 * Returns direct pointers into the ring buffer's backing store. No intermediate
 * copy buffers are allocated and no worker thread is created.
 *
 * ### Backpressure and memory safety
 * The ring buffer region covered by the current batch is "pinned": the reader
 * cursor is not advanced until release() is called. Because RecBuffer blocks
 * the writer once it reaches the minimum reader cursor, the writer cannot
 * overwrite pinned memory. A slow reader therefore directly stalls the writer.
 *
 * ### Pinning contract
 * - A batch becomes pinned the moment nextBatch() returns a valid result.
 * - It is unpinned by the first of: the next nextBatch() call (implicit release),
 *   or an explicit release() call.
 * - While pinned, the caller may safely read
 *   `batch.data[0 .. batch.count * batch.recordSize)`.
 *
 * ### Ring-buffer wrap-around
 * A single batch never crosses the physical end of the ring buffer. If fewer
 * records are contiguous than requested, the batch will be smaller; the
 * remaining records are returned in the next nextBatch() call.
 *
 * ### When to use
 * Use when the consumer is fast enough to keep pace with the writer and
 * allocations or copies are undesirable — real-time UI rendering, high-throughput
 * network senders, benchmarking pipelines.
 * For slow consumers (disk I/O, heavy processing) prefer RecordReader, which
 * decouples consumer processing speed from writer throughput.
 *
 * @see RecordReader
 */
class CYCLIB_EXPORT RecordReaderZC : public RecordReaderBase {
public:
    /// @brief Type alias — mirrors RecordReader::RecordBatch for uniform consumer code.
    using RecordBatch = RecordReaderBase::RecordBatch;

    /**
     * @brief Default constructor. Leaves the reader uninitialised.
     * Call init() before use.
     */
    RecordReaderZC();

    /**
     * @brief Constructs and initialises the reader.
     * @param target        Shared pointer to the source RecBuffer.
     * @param batchCapacity Maximum number of records to return per nextBatch() call.
     */
    RecordReaderZC(std::shared_ptr<RecBuffer> target, size_t batchCapacity);

    /** @brief Stops the reader and unregisters from the buffer. */
    ~RecordReaderZC() override;

    /**
     * @brief Initialises the reader. Must be called once on default-constructed instances.
     * @param target        Shared pointer to the source RecBuffer.
     * @param batchCapacity Maximum number of records to return per nextBatch() call.
     */
    void init(std::shared_ptr<RecBuffer> target, size_t batchCapacity);

    // -------------------------------------------------------------------------
    // RecordReaderBase overrides
    // -------------------------------------------------------------------------

    /** @brief Wakes the thread blocked in nextBatch() to re-check for available data. */
    void notifyDataAvailable() override;

    /**
     * @brief Releases any pinned batch and unblocks nextBatch() immediately.
     * @copydoc RecordReaderBase::stop
     */
    void stop() override;

    /**
     * @brief Marks the current write head as the drain target, then stops.
     * @copydoc RecordReaderBase::finish
     */
    void finish() override;

    /**
     * @brief Returns a zero-copy batch pointing directly into the ring buffer.
     *
     * Implicitly calls release() on the previous batch before requesting the next
     * one. The returned pointer is valid until the next nextBatch() or release().
     *
     * The batch count may be smaller than @p maxRecords when the requested range
     * would cross the physical end of the ring buffer; call nextBatch() again to
     * obtain the remainder.
     *
     * @copydoc RecordReaderBase::nextBatch
     */
    RecordBatch nextBatch(size_t maxRecords, bool wait = true) override;

    /**
     * @brief Unpins the current batch and advances the cursor.
     *
     * Advances the reader cursor to the end of the last returned batch, signalling
     * to the writer that the covered ring-buffer region may be reused. Safe to
     * call multiple times — subsequent calls before the next nextBatch() are no-ops.
     */
    void release() override;

private:
    uint64_t m_pinnedEnd = 0; ///< Cursor value at the end of the currently pinned batch.

    mutable std::mutex      m_mtx;
    std::condition_variable m_cv;

    std::atomic<bool>     m_finishing{false};   ///< @c true after finish() is called.
    std::atomic<uint64_t> m_finishTarget{0};    ///< Global cursor at which the reader should stop.
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_RECORDREADERZC_H
