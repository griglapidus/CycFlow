// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDREADER_H
#define CYC_RECORDREADER_H

#include "RecordReaderBase.h"
#include <thread>

namespace cyc {
CYCLIB_SUPPRESS_C4251

/**
 * @class RecordReader
 * @brief Asynchronous, double-buffered reader for RecBuffer.
 *
 * A dedicated background worker thread pre-fetches the next batch of records
 * from the ring buffer while the caller processes the current batch.
 * The reader cursor is advanced immediately after each copy, so writer
 * backpressure is independent of how long the caller takes to process data.
 *
 * ### Memory model
 * Two private heap buffers (A and B) alternate between "active" (being read by
 * the caller) and "background" (being filled by the worker). Pointers returned
 * by nextBatch() point into the active buffer and remain valid until the next
 * nextBatch() call.
 *
 * ### When to use
 * Prefer this class for slow consumers such as disk writers or any consumer
 * that may stall (network I/O with flow control, heavy processing).
 * For low-latency, zero-allocation cases see RecordReaderZC.
 *
 * @see RecordReaderZC
 */
class CYCLIB_EXPORT RecordReader : public RecordReaderBase {
public:
    /// @brief Type alias — preserves source compatibility for code written against
    ///        the pre-refactor RecordReader::RecordBatch name.
    using RecordBatch = RecordReaderBase::RecordBatch;

    /**
     * @brief Default constructor. Leaves the reader uninitialised.
     * Call init() before use.
     */
    RecordReader();

    /**
     * @brief Constructs and initialises the reader.
     * @param target        Shared pointer to the source RecBuffer.
     * @param batchCapacity Number of records to pre-fetch per background cycle.
     */
    RecordReader(std::shared_ptr<RecBuffer> target, size_t batchCapacity);

    /** @brief Stops the worker thread and unregisters from the buffer. */
    ~RecordReader() override;

    /**
     * @brief Initialises the reader. Must be called once on default-constructed instances.
     * @param target        Shared pointer to the source RecBuffer.
     * @param batchCapacity Number of records to pre-fetch per background cycle.
     */
    void init(std::shared_ptr<RecBuffer> target, size_t batchCapacity);

    // -------------------------------------------------------------------------
    // RecordReaderBase overrides
    // -------------------------------------------------------------------------

    /** @brief Wakes the prefetch worker thread to check for new data. */
    void notifyDataAvailable() override;

    /**
     * @brief Signals the worker thread to stop and blocks until it joins.
     * @copydoc RecordReaderBase::stop
     */
    void stop() override;

    /**
     * @brief Drains all buffered records up to the current write head, then stops.
     * @copydoc RecordReaderBase::finish
     */
    void finish() override;

    /**
     * @brief Returns the next pre-fetched batch.
     *
     * If the active buffer is exhausted, waits (when @p wait is @c true) for
     * the background worker to fill and swap in the next buffer.
     *
     * @copydoc RecordReaderBase::nextBatch
     */
    RecordBatch nextBatch(size_t maxRecords, bool wait = true) override;

private:
    void workerLoop();
    bool swapBuffers();

private:
    size_t m_activeIdx   = 0;   ///< Next record index within the active buffer.
    size_t m_activeCount = 0;   ///< Valid record count in the active buffer.

    std::vector<uint8_t>  m_bufferA;            ///< Primary copy buffer.
    std::vector<uint8_t>  m_bufferB;            ///< Secondary copy buffer.
    std::vector<uint8_t>* m_activeBuf = nullptr; ///< Buffer currently exposed to the caller.
    std::vector<uint8_t>* m_bgBuf     = nullptr; ///< Buffer currently being filled by the worker.

    std::thread             m_worker;
    std::mutex              m_mtx;
    std::condition_variable m_cv_user;   ///< Signals the caller when a new batch is ready.
    std::condition_variable m_cv_worker; ///< Signals the worker to fetch more data.

    size_t   m_bgCount      = 0;     ///< Records written into the background buffer.
    bool     m_bgIsFull     = false; ///< @c true when the background buffer is ready to swap.
    bool     m_finishing    = false; ///< @c true after finish() is called.
    uint64_t m_finishTarget = 0;     ///< Global cursor at which the worker should stop.
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_RECORDREADER_H
