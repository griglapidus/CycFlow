// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "CsvWriter.h"
#include "Core/RecRule.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <RecordReaderZC.h>

namespace cyc {

CsvWriter::CsvWriter() = default;

CsvWriter::CsvWriter(const std::string& filename, std::shared_ptr<RecBuffer> buffer,
                     bool autoStart, size_t batchSize, bool addTimestampSuffix)
    : CsvWriter()
{
    init(filename, buffer, autoStart, batchSize, addTimestampSuffix);
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

    // dtChar with count > 1 is a fixed-size string, emitted as a single quoted column.
    if (attr.type == DataType::dtChar && attr.count > 1) {
        m_file << "\"" << static_cast<char*>(ptr) << "\"";
        return;
    }

    const size_t elemSize = getTypeSize(attr.type);
    const size_t n        = attr.count > 0 ? attr.count : 1;
    auto*        base     = static_cast<uint8_t*>(ptr);

    for (size_t i = 0; i < n; ++i) {
        if (i > 0) m_file << m_delimiter;
        void* p = base + i * elemSize;
        switch (attr.type) {
        case DataType::dtBool:   m_file << (*static_cast<bool*>(p) ? "1" : "0"); break;
        case DataType::dtChar:   m_file << *static_cast<char*>(p); break;
        case DataType::dtInt8:   m_file << static_cast<int>(*static_cast<int8_t*>(p)); break;
        case DataType::dtUInt8:  m_file << static_cast<unsigned int>(*static_cast<uint8_t*>(p)); break;
        case DataType::dtInt16:  m_file << *static_cast<int16_t*>(p); break;
        case DataType::dtUInt16: m_file << *static_cast<uint16_t*>(p); break;
        case DataType::dtInt32:  m_file << *static_cast<int32_t*>(p); break;
        case DataType::dtUInt32: m_file << *static_cast<uint32_t*>(p); break;
        case DataType::dtInt64:  m_file << *static_cast<int64_t*>(p); break;
        case DataType::dtUInt64: m_file << *static_cast<uint64_t*>(p); break;
        case DataType::dtFloat:  m_file << *static_cast<float*>(p); break;
        case DataType::dtDouble: m_file << *static_cast<double*>(p); break;
        case DataType::dtPtr:    m_file << reinterpret_cast<uintptr_t>(*static_cast<void**>(p)); break;
        default: break;
        }
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
    bool first = true;
    for (const auto& attr : m_cachedAttrs) {
        // dtChar with count > 1 collapses to one column (a quoted string).
        const bool   isString = (attr.type == DataType::dtChar && attr.count > 1);
        const size_t n        = isString ? 1 : (attr.count > 0 ? attr.count : 1);
        for (size_t i = 0; i < n; ++i) {
            if (!first) ss << m_delimiter;
            first = false;
            ss << attr.name;
            if (n > 1) ss << '(' << i << ')';
        }
    }
    return ss.str();
}

std::string CsvWriter::createSuffixedFilename(const std::string &originalName) const {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t   = system_clock::to_time_t(now);
    const auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::tm buf;
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&buf, &t);
#else
    localtime_r(&t, &buf);
#endif

    char timeStr[32];
    std::strftime(timeStr, sizeof(timeStr), "_%Y-%m-%d_%H-%M-%S", &buf);

    std::ostringstream ss;
    ss << timeStr << '-' << std::setw(3) << std::setfill('0') << ms.count();
    const std::string suffix = ss.str();

    size_t dotPos = originalName.find_last_of('.');
    if (dotPos != std::string::npos && dotPos > 0) {
        return originalName.substr(0, dotPos) + suffix + originalName.substr(dotPos);
    }
    return originalName + suffix;
}

} // namespace cyc
