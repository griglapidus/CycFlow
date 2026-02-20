// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "CsvWriter.h"
#include "Core/RecRule.h"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <sstream>

namespace cyc {

CsvWriter::CsvWriter(const std::string& filename, std::shared_ptr<RecBuffer> buffer,
                     bool autoStart, size_t batchSize)
    : BatchRecordConsumer(buffer, batchSize)
    , m_filename(filename)
    , m_delimiter(",")
{
    m_cachedAttrs = getReader().getRule().getAttributes();

    if (autoStart) {
        start();
    }
}

CsvWriter::~CsvWriter() {
    stop();
}

void CsvWriter::onConsumeStart() {
    setupFile();

    if (m_file.is_open()) {
        m_file << std::fixed << std::setprecision(6);
    } else {
        std::cerr << "CsvWriter: Failed to open file " << m_filename << "\n";
    }
}

void CsvWriter::onConsumeStop() {
    if (m_file.is_open()) {
        m_file.flush();
        m_file.close();
    }
}

void CsvWriter::consumeBatch(const RecordReader::RecordBatch& batch) {
    if (!m_file.is_open()) return;

    // Fast-path iteration over the memory block
    for (size_t r = 0; r < batch.count; ++r) {
        Record rec(batch.rule, const_cast<uint8_t*>(batch.data + r * batch.recordSize));

        for (size_t i = 0; i < m_cachedAttrs.size(); ++i) {
            writeValue(rec, m_cachedAttrs[i]);

            if (i < m_cachedAttrs.size() - 1) {
                m_file << m_delimiter;
            }
        }
        m_file << "\n";
    }
}

void CsvWriter::writeValue(const Record& rec, const PAttr& attr) {
    void* ptr = rec.getVoid(attr.id);
    if (!ptr) return;

    switch (attr.type) {
    case DataType::dtBool:   m_file << (*static_cast<bool*>(ptr) ? "1" : "0"); break;
    case DataType::dtChar:
        if (attr.count > 1) {
            m_file << "\"" << static_cast<char*>(ptr) << "\"";
        } else {
            m_file << *static_cast<char*>(ptr);
        }
        break;
    case DataType::dtInt8:   m_file << static_cast<int>(*static_cast<int8_t*>(ptr)); break;
    case DataType::dtUInt8:  m_file << static_cast<unsigned int>(*static_cast<uint8_t*>(ptr)); break;
    case DataType::dtInt16:  m_file << *static_cast<int16_t*>(ptr); break;
    case DataType::dtUInt16: m_file << *static_cast<uint16_t*>(ptr); break;
    case DataType::dtInt32:  m_file << *static_cast<int32_t*>(ptr); break;
    case DataType::dtUInt32: m_file << *static_cast<uint32_t*>(ptr); break;
    case DataType::dtInt64:  m_file << *static_cast<int64_t*>(ptr); break;
    case DataType::dtUInt64: m_file << *static_cast<uint64_t*>(ptr); break;
    case DataType::dtFloat:  m_file << *static_cast<float*>(ptr); break;
    case DataType::dtDouble: m_file << *static_cast<double*>(ptr); break;
    case DataType::dtPtr:    m_file << reinterpret_cast<uintptr_t>(*static_cast<void**>(ptr)); break;
    default: break;
    }
}

void CsvWriter::setupFile() {
    std::string expectedHeader = generateHeader();
    std::ifstream inFile(m_filename);

    bool exists = inFile.is_open();
    bool headerMatches = false;
    bool isEmpty = true;

    if (exists) {
        std::string firstLine;
        if (std::getline(inFile, firstLine)) {
            isEmpty = false;
            // Handle cross-platform line endings
            if (!firstLine.empty() && firstLine.back() == '\r') {
                firstLine.pop_back();
            }
            headerMatches = (firstLine == expectedHeader);
        }
        inFile.close();
    }

    if (!exists || isEmpty) {
        m_file.open(m_filename, std::ios::out);
        m_file << expectedHeader << "\n";
        m_file.flush();
    } else if (headerMatches) {
        m_file.open(m_filename, std::ios::app);
    } else {
        m_filename = createSuffixedFilename(m_filename);
        m_file.open(m_filename, std::ios::out);
        m_file << expectedHeader << "\n";
        m_file.flush();
    }
}

std::string CsvWriter::generateHeader() const {
    std::stringstream ss;
    for (size_t i = 0; i < m_cachedAttrs.size(); ++i) {
        ss << m_cachedAttrs[i].name;
        if (i < m_cachedAttrs.size() - 1) {
            ss << m_delimiter;
        }
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

    size_t dotPos = originalName.find_last_of('.');
    if (dotPos != std::string::npos && dotPos > 0) {
        return originalName.substr(0, dotPos) + timeStr + originalName.substr(dotPos);
    }
    return originalName + timeStr;
}

} // namespace cyc
