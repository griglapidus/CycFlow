// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDWRITER_H
#define CYC_RECORDWRITER_H

#include "RecordWriterBase.h"
#include <thread>

namespace cyc {
CYCLIB_SUPPRESS_C4251

/**
 * @class RecordWriter
 * @brief Asynchronous, double-buffered writer for RecBuffer.
 *
 * A dedicated background worker thread flushes filled buffers to the target
 * RecBuffer while the producer writes into the alternate buffer. The cursor
 * advances asynchronously, so the producer is decoupled from buffer backpressure.
 *
 * ### Memory model
 * Two private heap buffers (A and B) alternate between "active" (written by the
 * caller via nextRecord() / nextBatch()) and "background" (being flushed to
 * RecBuffer by the worker). An early-flush threshold triggers a swap before the
 * active buffer is fully filled, reducing latency under steady load.
 *
 * ### When to use
 * Prefer this class for high-throughput producers that must never stall on
 * buffer backpressure. The decoupled flush thread absorbs reader latency spikes.
 * For simpler or lower-latency cases see RecordWriterZC.
 *
 * @see RecordWriterZC
 */
class CYCLIB_EXPORT RecordWriter : public RecordWriterBase {
public:
    /// @brief Type alias — preserves source compatibility for code written against
    ///        the pre-refactor RecordWriter::RecordBatch name.
    using RecordBatch = RecordWriterBase::RecordBatch;

    /**
     * @brief Default constructor. Leaves the writer uninitialised.
     * Call init() before use.
     */
    RecordWriter();

    /**
     * @brief Constructs and initialises the writer.
     * @param target        Shared pointer to the destination RecBuffer.
     * @param batchCapacity Number of records each intermediate buffer can hold.
     * @param blockOnFull   If @c true, the worker stalls when the target is full;
     *                      if @c false, old data is overwritten immediately.
     */
    RecordWriter(std::shared_ptr<RecBuffer> target, size_t batchCapacity, bool blockOnFull = true);

    /** @brief Flushes pending data and stops the worker thread. */
    ~RecordWriter() override;

    /**
     * @brief Initialises the writer. Must be called once on default-constructed instances.
     * @param target        Shared pointer to the destination RecBuffer.
     * @param batchCapacity Number of records each intermediate buffer can hold.
     * @param blockOnFull   If @c true, the worker stalls when the target is full.
     */
    void init(std::shared_ptr<RecBuffer> target, size_t batchCapacity, bool blockOnFull = true);

    // -------------------------------------------------------------------------
    // RecordWriterBase overrides
    // -------------------------------------------------------------------------

    /**
     * @brief Acquires the next record slot in the active buffer.
     *
     * If the active buffer is full, blocks until the background worker finishes
     * flushing the previous buffer and performs a swap.
     *
     * @copydoc RecordWriterBase::nextRecord
     */
    Record nextRecord() override;

    /**
     * @brief Timestamps the record and advances the active-buffer index.
     * @copydoc RecordWriterBase::commitRecord
     */
    void commitRecord() override;

    /**
     * @brief Acquires up to @p maxRecords contiguous slots in the active buffer.
     * @copydoc RecordWriterBase::nextBatch
     */
    RecordBatch nextBatch(size_t maxRecords, bool wait = true) override;

    /**
     * @brief Timestamps @p count records and advances the active-buffer index.
     * @copydoc RecordWriterBase::commitBatch
     */
    void commitBatch(size_t count) override;

    /**
     * @brief Forces all pending data into RecBuffer and waits for the worker to finish.
     * @copydoc RecordWriterBase::flush
     */
    void flush() override;

private:
    void shutdownWorker();  ///< Flushes remaining data and joins the worker thread.
    bool swapBuffers(bool blocking);
    void workerLoop();

private:
    size_t m_earlyThreshold = 0; ///< Active-buffer fill level that triggers an early swap.
    size_t m_currentIdx     = 0; ///< Next free slot index in the active buffer.

    std::vector<uint8_t>  m_bufferA;             ///< Primary intermediate buffer.
    std::vector<uint8_t>  m_bufferB;             ///< Secondary intermediate buffer.
    std::vector<uint8_t>* m_activeBuf = nullptr; ///< Buffer currently exposed to the caller.
    std::vector<uint8_t>* m_bgBuf     = nullptr; ///< Buffer currently being flushed by the worker.

    std::thread             m_worker;
    std::mutex              m_mtx;
    std::condition_variable m_cv;      ///< Signals the worker that a buffer is ready to flush.
    std::condition_variable m_cv_done; ///< Signals the caller that the worker finished flushing.

    size_t             m_bgCount  = 0;     ///< Records in the background buffer.
    std::atomic<bool>  m_running{false};   ///< @c true while the worker thread is active.
    bool               m_hasWork  = false; ///< @c true when the worker has a buffer to flush.
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_RECORDWRITER_H
