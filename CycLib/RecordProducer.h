// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDPRODUCER_H
#define CYC_RECORDPRODUCER_H

#include "Core/CycLib_global.h"
#include "Core/RecBuffer.h"
#include "RecordWriter.h"
#include "Core/RecRule.h"

namespace cyc {
CYCLIB_SUPPRESS_C4251

/**
 * @class RecordProducer
 * @brief Abstract base class for generating data records.
 *
 * Lazily initializes a RecBuffer and a RecordWriter based on the schema
 * provided by the subclass via `defineRule()`. Manages a background thread
 * to continuously produce data.
 */
class CYCLIB_EXPORT RecordProducer {
public:
    /**
     * @brief Constructor.
     * @note RecBuffer is NOT created here. It is lazily instantiated on first use.
     * @param bufferCapacity Number of records the circular buffer can hold.
     * @param writerBatchSize Batch size for the internal RecordWriter.
     */
    RecordProducer(size_t bufferCapacity = 10000, size_t writerBatchSize = 100);
    virtual ~RecordProducer();

    /**
     * @brief Starts the background production thread.
     * Automatically triggers initialization if not already done.
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
     * @brief Provides access to the underlying RecBuffer.
     * Triggers lazy initialization if called for the first time.
     * @return Shared pointer to the buffer.
     */
    std::shared_ptr<RecBuffer> getBuffer();

    /**
     * @brief Provides access to the data writer.
     * Triggers lazy initialization if called for the first time.
     * @return Reference to the internal RecordWriter.
     */
    RecordWriter& getWriter();

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
     * @param rec Pre-allocated record to be filled with data.
     * @return True to continue production, false to stop the thread.
     */
    virtual bool produceStep(Record& rec) = 0;

    /**
     * @brief Lifecycle hook called just before the main loop starts.
     */
    virtual void onProduceStart() {}

    /**
     * @brief Lifecycle hook called immediately after the main loop terminates.
     */
    virtual void onProduceStop() {}

    virtual void workerLoop();

private:
    /**
     * @brief Thread-safe lazy initialization of the buffer and writer.
     */
    void initialize();

protected:
    size_t m_bufferCapacity;
    size_t m_writerBatchSize;

    std::shared_ptr<RecBuffer> m_buffer;
    std::unique_ptr<RecordWriter> m_writer;

    std::atomic<bool> m_running;
    std::thread m_worker;

    std::mutex m_initMtx;
    std::atomic<bool> m_isInitialized;
};

/**
 * @class BatchRecordProducer
 * @brief Optimized producer class for generating data in large contiguous blocks.
 */
class CYCLIB_EXPORT BatchRecordProducer : public RecordProducer {
public:
    using RecordProducer::RecordProducer;

protected:
    /**
     * @brief Blocked single-record production method.
     * Marked as final to prevent misuse in batch mode.
     */
    bool produceStep(Record& rec) override final { return false; }

    /**
     * @brief Generates a batch of records.
     * Must be implemented by the derived class.
     * @param batch Memory block to be filled with records.
     * @return The actual number of records written to the batch.
     * Returning 0 will yield the thread temporarily.
     */
    virtual size_t produceBatch(const RecordWriter::RecordBatch& batch) = 0;

    void workerLoop() override;
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_RECORDPRODUCER_H
