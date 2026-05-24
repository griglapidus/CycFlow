// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CSVWRITER_H
#define CYC_CSVWRITER_H

#include "RecordConsumer.h"
#include "RecordReaderZC.h"
#include "Core/PAttr.h"
#include <atomic>
#include <fstream>

namespace cyc {
CYCLIB_SUPPRESS_C4251
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
     * @brief Default constructor. Leaves the writer uninitialised.
     * Call init() before using the writer.
     */
    CsvWriter();

    /**
     * @brief Constructs the CSV writer with the default RecordReader (double-buffered).
     *
     * @param maxRecords  Rotate to a new file after this many records (0 = disabled).
     */
    CsvWriter(const std::string& filename, std::shared_ptr<RecBuffer> buffer,
              bool autoStart = true, size_t batchSize = 100,
              bool addTimestampSuffix = true, size_t maxRecords = 0);

    /**
     * @brief Constructs the CSV writer with an explicitly chosen reader type.
     * @code
     * CsvWriter writer(UseReader<RecordReaderZC>{}, "out.csv", buffer);
     * @endcode
     */
    template<typename ReaderType>
    CsvWriter(UseReader<ReaderType>, const std::string& filename,
              std::shared_ptr<RecBuffer> buffer,
              bool autoStart = true, size_t batchSize = 100,
              bool addTimestampSuffix = true, size_t maxRecords = 0)
        : CsvWriter()
    {
        init(UseReader<ReaderType>{}, filename, buffer, autoStart, batchSize, addTimestampSuffix, maxRecords);
    }

    /**
     * @brief Destructor. Ensures the thread is stopped and the file is closed.
     */
    ~CsvWriter() override;

    /**
     * @brief Initialises the writer with the default RecordReaderZC (zero-copy).
     * Must be called once on default-constructed instances.
     *
     * @param filename             Output CSV path.
     * @param buffer               Shared pointer to the source RecBuffer.
     * @param autoStart            If @c true, starts the worker thread immediately.
     * @param batchSize            Batch size for the internal reader.
     * @param addTimestampSuffix   If @c true, the current local time (with
     *                             millisecond precision) is inserted before the
     *                             extension: @c "Foo.csv" → @c "Foo_2026-05-03_19-51-15-022.csv".
     * @param maxRecords           Rotate to a new file after this many records (0 = disabled).
     */
    void init(const std::string& filename, std::shared_ptr<RecBuffer> buffer,
              bool autoStart = true, size_t batchSize = 100,
              bool addTimestampSuffix = true, size_t maxRecords = 0) {
        init(UseReader<RecordReaderZC>{}, filename, buffer, autoStart, batchSize, addTimestampSuffix, maxRecords);
    }

    /**
     * @brief Initialises the writer with an explicitly chosen reader type.
     *
     * @tparam ReaderType          Any class derived from RecordReaderBase.
     * @param filename             Output CSV path.
     * @param buffer               Shared pointer to the source RecBuffer.
     * @param autoStart            If @c true, starts the worker thread immediately.
     * @param batchSize            Batch size for the internal reader.
     * @param addTimestampSuffix   If @c true, the current local time (with
     *                             millisecond precision) is inserted before the
     *                             extension: @c "Foo.csv" → @c "Foo_2026-05-03_19-51-15-022.csv".
     * @param maxRecords           Rotate to a new file after this many records (0 = disabled).
     * @code
     * writer.init(UseReader<RecordReader>{}, "out.csv", buffer);
     * @endcode
     */
    template<typename ReaderType>
    void init(UseReader<ReaderType>, const std::string& filename, std::shared_ptr<RecBuffer> buffer,
              bool autoStart = true, size_t batchSize = 100,
              bool addTimestampSuffix = true, size_t maxRecords = 0) {
        m_baseFilename = filename;
        m_addTimestampSuffix = addTimestampSuffix;
        m_maxRecords = maxRecords;
        m_recordCount = 0;
        m_filename = addTimestampSuffix ? createSuffixedFilename(filename) : filename;
        RecordConsumer::init(UseReader<ReaderType>{}, buffer, batchSize);
        m_cachedAttrs = getReader().getRule().getAttributes();
        if (autoStart) start();
    }

    /**
     * @brief Closes the current file and begins writing to a new one.
     *
     * Thread-safe: may be called from any thread while the writer is running.
     * The actual rotation happens at the start of the next batch, so there is
     * no data loss — all records written before the call land in the old file.
     */
    void restart();

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
    void writeValue(const Record& rec, const PAttr& attr);
    void setupFile();
    void rotateFile();
    [[nodiscard]] std::string generateHeader() const;
    [[nodiscard]] std::string createSuffixedFilename(const std::string& originalName) const;

private:
    std::string   m_baseFilename;
    std::string   m_filename;
    bool          m_addTimestampSuffix = true;
    std::string   m_delimiter = ",";
    std::ofstream m_file;
    std::vector<PAttr> m_cachedAttrs;
    size_t        m_maxRecords  = 0;
    size_t        m_recordCount = 0;
    std::atomic<bool> m_restartRequested{false};
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_CSVWRITER_H
