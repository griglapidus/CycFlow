// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORDCONSUMER_H
#define CYC_RECORDCONSUMER_H

#include "Core/CycLib_global.h"
#include "RecordReader.h"
#include <atomic>
#include <thread>
#include <memory>

namespace cyc {

/**
 * @brief Base class for consuming records from RecBuffer asynchronously.
 *
 * It manages the AsyncRecordReader and a background worker thread.
 * Concrete implementations (like CsvWriter) must override consumeRecord().
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
     * @brief Stops the consumption thread immediately.
     */
    void stop();

    /**
     * @brief Consumes all remaining records up to the current point, then stops.
     */
    void finish();

    bool isRunning() const;

protected:
    /**
     * @brief Lifecycle hook: Called inside the thread before the loop starts.
     */
    virtual void onConsumeStart() {}

    /**
     * @brief Pure virtual method to process a single record.
     * @param rec The record fetched from the buffer.
     */
    virtual void consumeRecord(const Record& rec) = 0;

    /**
     * @brief Lifecycle hook: Called inside the thread after the loop ends.
     */
    virtual void onConsumeStop() {}

    const RecordReader& getReader() const;

    virtual void workerLoop();

protected:
    std::unique_ptr<RecordReader> m_reader;
    std::atomic_bool m_running;
    std::thread m_worker;
};

class CYCLIB_EXPORT BatchRecordConsumer : public RecordConsumer {
public:
    using RecordConsumer::RecordConsumer; // Используем конструктор базового класса

protected:
    /**
     * @brief Заглушка для поштучного метода.
     * Помечена final, чтобы никто случайно не попытался её использовать.
     */
    void consumeRecord(const Record& rec) override final {}

    /**
     * @brief Новый чисто виртуальный метод для обработки пакетов.
     * Наследник ОБЯЗАН реализовать именно его.
     */
    virtual void consumeBatch(const RecordReader::RecordBatch& batch) = 0;

    /**
     * @brief Переопределенный цикл, работающий с пакетами.
     */
    void workerLoop() override;
};

} // namespace cyc

#endif // CYC_RECORDCONSUMER_H
