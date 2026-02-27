// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDCONSUMER_H
#define CYC_RECORDCONSUMER_H

#include "Core/CycLib_global.h"
#include "RecordReader.h"

namespace cyc {

/**
 * @class RecordConsumer
 * @brief Base class for consuming records from RecBuffer asynchronously.
 *
 * Manages an AsyncRecordReader and a background worker thread.
 * Concrete implementations (e.g., CsvWriter) must override consumeRecord().
 */
class CYCLIB_EXPORT RecordConsumer {
public:
    RecordConsumer(std::shared_ptr<RecBuffer> buffer, size_t readerBatchSize = 100);
    virtual ~RecordConsumer();

    /**
     * @brief Starts the consumption thread.
     */
    void start();

    /**
     * @brief Stops the consumption thread immediately without waiting for remaining data.
     */
    void stop();

    /**
     * @brief Consumes all remaining records up to the current buffer cursor, then stops.
     */
    void finish();

    [[nodiscard]] bool isRunning() const;

protected:
    /**
     * @brief Lifecycle hook: Called inside the thread before the main loop starts.
     */
    virtual void onConsumeStart() {}

    /**
     * @brief Processes a single record fetched from the buffer.
     * Must be implemented by the derived class.
     * @param rec Read-only record view.
     */
    virtual void consumeRecord(const Record& rec) = 0;

    /**
     * @brief Lifecycle hook: Called inside the thread after the main loop terminates.
     */
    virtual void onConsumeStop() {}

    [[nodiscard]] const RecordReader& getReader() const;

    virtual void workerLoop();

protected:
    std::unique_ptr<RecordReader> m_reader;
    std::atomic<bool> m_running;
    std::thread m_worker;
};

/**
 * @class BatchRecordConsumer
 * @brief Optimized consumer class for processing data in large contiguous blocks.
 */
class CYCLIB_EXPORT BatchRecordConsumer : public RecordConsumer {
public:
    using RecordConsumer::RecordConsumer;

protected:
    /**
     * @brief Blocked single-record consumption method.
     * Marked as final to prevent misuse in batch mode.
     */
    void consumeRecord(const Record& rec) override final {}

    /**
     * @brief Processes a batch of records at once.
     * Must be implemented by the derived class.
     * @param batch A contiguous memory block containing multiple records.
     */
    virtual void consumeBatch(const RecordReader::RecordBatch& batch) = 0;

    void workerLoop() override;
};

} // namespace cyc

#endif // CYC_RECORDCONSUMER_H
