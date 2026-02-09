// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CSVWRITER_H
#define CYC_CSVWRITER_H

#include "CycLib_global.h"
#include "RecordReader.h"
#include <string>
#include <fstream>
#include <atomic>
#include <thread>

namespace cyc {

/**
 * @brief Writes records from AsyncRecordReader to a CSV file in a background thread.
 *
 * This class spawns a worker thread that continuously fetches records from
 * the provided reader and appends them to the specified CSV file.
 */
class CYCLIB_EXPORT CsvWriter {
public:
    /**
     * @brief Constructs the writer.
     * @param filename Path to the output CSV file.
     * @param reader Reference to the source AsyncRecordReader.
     * @param autoStart If true, the worker thread starts immediately.
     */
    CsvWriter(const std::string& filename, std::shared_ptr<RecBuffer> buffer,
              bool autoStart = true, size_t readerBatchSize = 100);

    /**
     * @brief Destructor.
     * Stops the thread and flushes pending data.
     */
    ~CsvWriter();

    /**
     * @brief Starts the background writing thread.
     * Does nothing if the thread is already running.
     */
    void start();

    /**
     * @brief Stops the background thread.
     * Blocks until the current write operation finishes and the thread joins.
     */
    void stop();

    /**
     * @brief Initiates graceful shutdown.
     * Tells the reader to consume all currently written data, waits for it to finish processing,
     * and then stops the thread.
     */
    void extracted();
    void finish();

    /**
     * @brief Checks if the writer is currently running.
     */
    bool isRunning() const;

private:
    /**
     * @brief Main loop executed by the background thread.
     */
    void workerLoop();

    /**
     * @brief Writes the CSV header row if the file is new/empty.
     */
    void writeHeader();

    /**
     * @brief Formats and writes a single field value.
     */
    void writeValue(std::ofstream& file, const Record& rec, const PAttr& attr);

    /**
     * @brief Prepares the output file for writing.
     */
    void setupFile();

    /**
     * @brief Generates the CSV header line based on the record schema.
     * @return A string containing the header row (e.g., "Time,Value,Status\n").
     */
    std::string generateHeader() const;

    /**
     * @brief Generates a unique filename by appending a suffix to the original name.
     * @param originalName The base filename provided in the constructor.
     * @return A new filename string with the suffix inserted before the extension.
     */
    std::string createSuffixedFilename(const std::string& originalName) const;

private:
    std::string m_filename;
    std::string m_delimiter;

    std::ofstream m_file;

    std::unique_ptr<AsyncRecordReader> m_reader;
    std::shared_ptr<RecBuffer> m_buffer;

    std::atomic_bool m_running;
    std::thread m_worker;
};

} // namespace cyc

#endif // CYC_CSVWRITER_H
