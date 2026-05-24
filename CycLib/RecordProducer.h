// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDPRODUCER_H
#define CYC_RECORDPRODUCER_H

#include "Core/CycLib_global.h"
#include "Core/RecBuffer.h"
#include "RecordWriter.h"      // default writer type + RecordWriter::RecordBatch alias
#include "Core/RecRule.h"
#include <functional>

namespace cyc {
CYCLIB_SUPPRESS_C4251

/**
 * @class RecordProducer
 * @brief Abstract base class for generating records into a RecBuffer.
 *
 * Lazily initialises a RecBuffer and a RecordWriterBase-derived writer based on
 * the schema provided by the subclass via defineRule(). Manages a background
 * thread that calls produceStep() in a tight loop.
 *
 * ### Writer type selection
 * By default a RecordWriter (double-buffered, async) is created. To use a
 * different writer pass a UseWriter<T> tag as the first constructor argument,
 * or call init() before start():
 * @code
 * // Via constructor
 * MyProducer producer(UseWriter<RecordWriterZC>{}, 10000, 100);
 *
 * // Via init (must be called before start())
 * producer.init(UseWriter<RecordWriterZC>{}, 10000, 100);
 * @endcode
 *
 * @see BatchRecordProducer
 * @see RecordWriter
 * @see RecordWriterZC
 */
class CYCLIB_EXPORT RecordProducer {
public:
    /**
     * @brief Constructs the producer with the default RecordWriter (double-buffered).
     * @param bufferCapacity  Number of records the ring buffer can hold.
     * @param writerBatchSize Batch size for the internal writer.
     */
    RecordProducer(size_t bufferCapacity = 10000, size_t writerBatchSize = 100);

    /**
     * @brief Constructs the producer with an explicitly chosen writer type.
     * @code
     * MyProducer p(UseWriter<RecordWriterZC>{}, 10000, 100);
     * @endcode
     */
    template<typename WriterType>
    RecordProducer(UseWriter<WriterType>, size_t bufferCapacity = 10000, size_t writerBatchSize = 100)
        : RecordProducer()
    {
        init(UseWriter<WriterType>{}, bufferCapacity, writerBatchSize);
    }

    virtual ~RecordProducer();

    /**
     * @brief Configures the producer with the default RecordWriter (double-buffered).
     *
     * Must be called before start() on default-constructed instances.
     * @param bufferCapacity  Number of records the ring buffer can hold.
     * @param writerBatchSize Batch size for the internal writer.
     */
    void init(size_t bufferCapacity = 10000, size_t writerBatchSize = 100) {
        m_bufferCapacity  = bufferCapacity;
        m_writerBatchSize = std::min(std::max(writerBatchSize, bufferCapacity / 20), bufferCapacity);
        m_writerFactory   = [](std::shared_ptr<RecBuffer> buf, size_t batch) {
            return std::make_unique<RecordWriter>(buf, batch, true);
        };
    }

    /**
     * @brief Configures the producer with an explicitly chosen writer type.
     *
     * @tparam WriterType  Writer implementation to use.
     * @param bufferCapacity  Number of records the ring buffer can hold.
     * @param writerBatchSize Batch size for the internal writer.
     * @code
     * producer.init(UseWriter<RecordWriterZC>{}, 10000, 100);
     * @endcode
     */
    template<typename WriterType>
    void init(UseWriter<WriterType>, size_t bufferCapacity = 10000, size_t writerBatchSize = 100) {
        m_bufferCapacity  = bufferCapacity;
        m_writerBatchSize = std::min(std::max(writerBatchSize, bufferCapacity / 20), bufferCapacity);
        m_writerFactory   = [](std::shared_ptr<RecBuffer> buf, size_t batch) {
            return std::make_unique<WriterType>(buf, batch, true);
        };
    }

    /**
     * @brief Starts the background production thread.
     * Triggers lazy initialisation of the buffer and writer on first call.
     */
    void start();

    /**
     * @brief Stops the production thread and flushes remaining data.
     */
    void stop();

    /**
     * @brief Blocks until the background thread finishes execution.
     */
    void join();

    [[nodiscard]] bool isRunning() const;

    /**
     * @brief Returns the underlying RecBuffer, triggering lazy init if needed.
     * @return Shared pointer to the buffer.
     */
    std::shared_ptr<RecBuffer> getBuffer();

    /**
     * @brief Returns the internal writer, triggering lazy init if needed.
     * @return Reference to the RecordWriterBase instance.
     */
    RecordWriterBase& getWriter();

protected:
    /**
     * @brief Defines the schema for the generated records.
     * Must be implemented by the derived class.
     * @return RecRule describing the record layout.
     */
    virtual RecRule defineRule() = 0;

    /**
     * @brief Generates a single record.
     * Must be implemented by the derived class.
     * @param rec Pre-allocated record to fill with data.
     * @return @c true to continue production, @c false to stop the thread.
     */
    virtual bool produceStep(Record& rec) = 0;

    /** @brief Lifecycle hook called just before the main loop starts. */
    virtual void onProduceStart() {}

    /** @brief Lifecycle hook called immediately after the main loop terminates. */
    virtual void onProduceStop() {}

    virtual void workerLoop();

private:
    /** @brief Thread-safe lazy initialisation of the buffer and writer. */
    void initialize();

protected:
    size_t m_bufferCapacity  = 0; ///< Ring buffer capacity in records.
    size_t m_writerBatchSize = 0; ///< Clamped writer batch size.

    std::shared_ptr<RecBuffer>        m_buffer;
    std::unique_ptr<RecordWriterBase> m_writer;

    std::atomic<bool> m_running{false};
    std::thread       m_worker;

    std::mutex        m_initMtx;
    std::atomic<bool> m_isInitialized{false};

    /// Factory function set by init(). Called lazily by initialize().
    std::function<std::unique_ptr<RecordWriterBase>(std::shared_ptr<RecBuffer>, size_t)> m_writerFactory;
};

/**
 * @class BatchRecordProducer
 * @brief Producer variant for generating records in large contiguous blocks.
 *
 * Overrides the worker loop to call produceBatch() instead of produceStep(),
 * reducing virtual-call overhead for high-throughput batch producers.
 */
class CYCLIB_EXPORT BatchRecordProducer : public RecordProducer {
public:
    using RecordProducer::RecordProducer;

protected:
    /**
     * @brief Blocked single-record method. Marked final to prevent misuse.
     */
    bool produceStep(Record& rec) override final { return false; }

    /**
     * @brief Generates a batch of records into the provided memory block.
     *
     * The parameter type is RecordWriter::RecordBatch which is an alias for
     * RecordWriterBase::RecordBatch — both names refer to the same type.
     *
     * @param batch Writable memory block to fill. batch.capacity is the upper bound.
     * @return The actual number of records written. Returning 0 yields the thread briefly.
     */
    virtual size_t produceBatch(const RecordWriter::RecordBatch& batch) = 0;

    void workerLoop() override;
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_RECORDPRODUCER_H
