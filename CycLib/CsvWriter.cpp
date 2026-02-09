// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "CsvWriter.h"
#include "RecRule.h"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <sstream>

namespace cyc {

CsvWriter::CsvWriter(const std::string& filename, std::shared_ptr<RecBuffer> buffer,
                     bool autoStart, size_t readerBatchSize)
    : RecordConsumer(buffer, readerBatchSize) // Initialize base class
    , m_filename(filename)
    , m_delimiter(",")
{
    // Cache attributes from the reader's rule for performance
    m_cachedAttrs = getReader().getRule().getAttributes();

    setupFile();

    if (m_file.is_open()) {
        m_file << std::fixed << std::setprecision(6);
    }

    if (autoStart) {
        start();
    }
}

CsvWriter::~CsvWriter() {
    // Stop the thread explicitly before closing the file
    stop();

    if (m_file.is_open()) {
        m_file.flush();
        m_file.close();
    }
}

void CsvWriter::consumeRecord(const Record& rec) {
    if (!m_file.is_open()) return;

    for (size_t i = 0; i < m_cachedAttrs.size(); ++i) {
        writeValue(m_file, rec, m_cachedAttrs[i]);
        if (i < m_cachedAttrs.size() - 1) {
            m_file << m_delimiter;
        }
    }
    m_file << "\n";
}

void CsvWriter::onConsumeStop() {
    if (m_file.is_open()) {
        m_file.flush();
    }
}

void CsvWriter::writeValue(std::ofstream& file, const Record& rec, const PAttr& attr) {
    switch (attr.type) {
    case DataType::dtBool:
        file << (rec.getBool(attr.id) ? "1" : "0");
        break;
    case DataType::dtChar:
        if (attr.count > 1) {
            file << "\"" << rec.getCharPtr(attr.id) << "\"";
        } else {
            file << rec.getChar(attr.id);
        }
        break;
    case DataType::dtInt8:   file << (int)rec.getInt8(attr.id); break;
    case DataType::dtUInt8:  file << (int)rec.getUInt8(attr.id); break;
    case DataType::dtInt16:  file << rec.getInt16(attr.id); break;
    case DataType::dtUInt16: file << rec.getUInt16(attr.id); break;
    case DataType::dtInt32:  file << rec.getInt32(attr.id); break;
    case DataType::dtUInt32: file << rec.getUInt32(attr.id); break;
    case DataType::dtInt64:  file << rec.getInt64(attr.id); break;
    case DataType::dtUInt64: file << rec.getUInt64(attr.id); break;
    case DataType::dtFloat:  file << rec.getFloat(attr.id); break;
    case DataType::dtDouble: file << rec.getDouble(attr.id); break;
    default: break;
    }
}

void CsvWriter::setupFile() {
    std::string expectedHeader = generateHeader();

    bool fileExists = false;
    bool headerMatches = false;
    bool isEmpty = true;

    {
        std::ifstream inFile(m_filename);
        if (inFile.good()) {
            fileExists = true;
            if (inFile.peek() != std::ifstream::traits_type::eof()) {
                isEmpty = false;
                std::string firstLine;
                std::getline(inFile, firstLine);
                // Handle potential Windows line endings
                if (!firstLine.empty() && firstLine.back() == '\r') {
                    firstLine.pop_back();
                }
                if (firstLine == expectedHeader) {
                    headerMatches = true;
                }
            }
        }
    }

    if (!fileExists || isEmpty) {
        m_file.open(m_filename, std::ios::out);
        m_file << expectedHeader << "\n";
        m_file.flush();
    } else if (headerMatches) {
        m_file.open(m_filename, std::ios::app);
    } else {
        // File exists but header mismatch -> create new file with timestamp suffix
        std::string newFilename = createSuffixedFilename(m_filename);
        m_filename = newFilename;
        m_file.open(m_filename, std::ios::out);
        m_file << expectedHeader << "\n";
        m_file.flush();
    }
}

std::string CsvWriter::generateHeader() const {
    std::stringstream ss;
    for (size_t i = 0; i < m_cachedAttrs.size(); ++i) {
        ss << m_cachedAttrs[i].name << (i < m_cachedAttrs.size() - 1 ? "," : "");
    }
    return ss.str();
}

std::string CsvWriter::createSuffixedFilename(const std::string &originalName) const {
    std::time_t now = std::time(nullptr);
    std::tm buf;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&buf, &now);
#else
    localtime_r(&now, &buf);
#endif

    char timeStr[32];
    std::strftime(timeStr, sizeof(timeStr), "_%Y%m%d_%H%M%S", &buf);

    size_t lastDot = originalName.find_last_of('.');
    if (lastDot == std::string::npos) {
        return originalName + timeStr;
    } else {
        std::string base = originalName.substr(0, lastDot);
        std::string ext = originalName.substr(lastDot);
        return base + timeStr + ext;
    }
}

} // namespace cyc
