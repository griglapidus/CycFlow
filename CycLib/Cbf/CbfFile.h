// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_CBFFILE_H
#define CYC_CBFFILE_H

#include "Core/CycLib_global.h"
#include "Core/RecRule.h"
#include "Core/Record.h"
#include "CbfDefs.h"

#include <fstream>

namespace cyc {

enum class CbfMode {
    Read,
    Write
};

/**
 * @class CbfFile
 * @brief Low-level wrapper for handling Cyc Binary Format (.cbf) files.
 *
 * Provides safe boundary checks, section header management, and raw byte stream
 * operations for writing and reading RecRules and raw records.
 */
class CYCLIB_EXPORT CbfFile {
public:
    CbfFile();
    ~CbfFile();

    bool open(const std::string& filename, CbfMode mode);
    void close();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] bool isGood() const;

    // --- Write API ---

    /**
     * @brief Sets an alias that will be written into the header.name of each section.
     * @param alias The alias name (max 10 characters).
     */
    void setAlias(const std::string& alias);

    /**
     * @brief Writes a Header section containing the given RecRule.
     */
    bool writeHeader(const RecRule& rule);

    /**
     * @brief Starts a Data section. The length is initially 0 and updated in endDataSection().
     */
    bool beginDataSection();

    /**
     * @brief Writes a single record to the data section.
     * @param rec The record to write.
     * @return True if successful.
     */
    bool writeRecord(const Record& rec);

    /**
     * @brief Writes raw bytes directly to the file.
     * @param data Pointer to the memory block.
     * @param size Size in bytes.
     */
    bool writeBytes(const void* data, size_t size);

    /**
     * @brief Finalizes the current Data section by updating its length field.
     */
    bool endDataSection();

    // --- Read API ---

    /**
     * @brief Reads the next section header from the stream.
     */
    bool readSectionHeader(CbfSectionHeader& header);

    /**
     * @brief Reads a Header section body and reconstructs a RecRule.
     */
    bool readRule(const CbfSectionHeader& header, RecRule& outRule);

    /**
     * @brief Reads a single record from the data section.
     * @param rec The record object with pre-allocated memory to read into.
     * @return True if successful.
     */
    bool readRecord(Record& rec);

    /**
     * @brief Skips the body of the given section to move to the next header.
     */
    bool skipSection(const CbfSectionHeader& header);

    /**
     * @brief Exposes the underlying filestream for zero-copy block reads.
     */
    std::fstream& getStream() { return m_file; }

private:
    std::fstream m_file;
    std::string m_filename;
    CbfMode m_mode;
    std::string m_alias;

    std::streampos m_currentSectionStart;
    size_t m_writtenBytesInSection;
};

} // namespace cyc

#endif // CYC_CBFFILE_H
