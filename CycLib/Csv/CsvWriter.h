// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CSVWRITER_H
#define CYC_CSVWRITER_H

#include "RecordConsumer.h"
#include "Core/PAttr.h"
#include <fstream>

namespace cyc {

/**
 * @class CsvWriter
 * @brief Writes data records from a RecBuffer to a CSV file.
 *
 * Inherits from BatchRecordConsumer to process records in bulk, drastically
 * reducing virtual call overhead. Formats memory blocks as comma-separated
 * values and writes them to the specified output file.
 */
class CYCLIB_EXPORT CsvWriter : public BatchRecordConsumer {
public:
    /**
     * @brief Constructs the CSV writer.
     * @param filename Path to the output CSV file.
     * @param buffer Shared pointer to the source RecBuffer.
     * @param autoStart If true, the worker thread starts immediately.
     * @param batchSize Number of records to read and process in one iteration.
     */
    CsvWriter(const std::string& filename, std::shared_ptr<RecBuffer> buffer,
              bool autoStart = true, size_t batchSize = 100);

    /**
     * @brief Destructor. Ensures the thread is stopped and the file is closed.
     */
    ~CsvWriter() override;

protected:
    /**
     * @brief Processes a batch of records in a tight loop.
     * @param batch The contiguous block of memory containing records.
     */
    void consumeBatch(const RecordReader::RecordBatch& batch) override;

    /**
     * @brief Called by the worker thread just before the main loop starts.
     * Opens the file and writes the header if necessary.
     */
    void onConsumeStart() override;

    /**
     * @brief Called by the worker thread just after the loop terminates.
     * Flushes and closes the file safely.
     */
    void onConsumeStop() override;

private:
    /**
     * @brief Formats and writes a single typed field value directly to the stream.
     * @param rec The record containing the field.
     * @param attr The attribute metadata describing the field.
     */
    void writeValue(const Record& rec, const PAttr& attr);

    /**
     * @brief Prepares the output file for writing (creates new or appends).
     */
    void setupFile();

    /**
     * @brief Generates the CSV header line based on the record schema.
     * @return Formatted header string.
     */
    [[nodiscard]] std::string generateHeader() const;

    /**
     * @brief Generates a unique filename using a timestamp if a header mismatch occurs.
     * @param originalName The base filename.
     * @return A new filename appended with the current timestamp.
     */
    [[nodiscard]] std::string createSuffixedFilename(const std::string& originalName) const;

private:
    std::string m_filename;
    std::string m_delimiter;
    std::ofstream m_file;
    std::vector<PAttr> m_cachedAttrs;
};

} // namespace cyc

#endif // CYC_CSVWRITER_H
