// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CSVWRITER_H
#define CYC_CSVWRITER_H

#include "RecordConsumer.h"
#include "Core/PAttr.h"
#include <string>
#include <fstream>
#include <vector>

namespace cyc {

/**
 * @brief Writes records from RecBuffer to a CSV file.
 *
 * Inherits thread management and reader interaction from RecordConsumer.
 * This class focuses solely on file I/O and CSV formatting.
 */
class CYCLIB_EXPORT CsvWriter : public RecordConsumer {
public:
    /**
     * @brief Constructs the CSV writer.
     * @param filename Path to the output CSV file.
     * @param buffer Shared pointer to the source RecBuffer.
     * @param autoStart If true, the worker thread starts immediately.
     * @param readerBatchSize Number of records to read in one batch (passed to base class).
     */
    CsvWriter(const std::string& filename, std::shared_ptr<RecBuffer> buffer,
              bool autoStart = true, size_t readerBatchSize = 100);

    /**
     * @brief Destructor.
     * Ensures the file is flushed and closed properly.
     */
    ~CsvWriter() override;

protected:
    /**
     * @brief Processes a single record fetched by the base class.
     * formats the record as a CSV line and writes it to the file.
     * @param rec The record to write.
     */
    void consumeRecord(const Record& rec) override;

    /**
     * @brief Called by the worker thread just before it stops.
     * Used here to flush the file stream.
     */
    void onConsumeStop() override;

private:
    /**
     * @brief Formats and writes a single field value.
     */
    void writeValue(std::ofstream& file, const Record& rec, const PAttr& attr);

    /**
     * @brief Prepares the output file for writing (creates or appends).
     */
    void setupFile();

    /**
     * @brief Generates the CSV header line based on the record schema.
     */
    std::string generateHeader() const;

    /**
     * @brief Generates a unique filename if the original file exists and has a mismatching header.
     */
    std::string createSuffixedFilename(const std::string& originalName) const;

private:
    std::string m_filename;
    std::string m_delimiter;
    std::ofstream m_file;

    // Local cache of attributes to avoid querying the rule/reader for every record
    std::vector<PAttr> m_cachedAttrs;
};

} // namespace cyc

#endif // CYC_CSVWRITER_H
