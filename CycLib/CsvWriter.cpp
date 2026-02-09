// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "CsvWriter.h"
#include "RecRule.h"
#include "PAttr.h"
#include <iostream>
#include <iomanip>

namespace cyc {

CsvWriter::CsvWriter(const std::string& filename,  std::shared_ptr<RecBuffer> buffer,
                     bool autoStart, size_t readerBatchSize)
    : m_filename(filename)
    , m_delimiter(",")
    , m_buffer(buffer)
    , m_running(false)
{
    m_reader = std::make_unique<AsyncRecordReader>(m_buffer, readerBatchSize);
    setupFile();
    if (m_file.is_open()) {
        m_file << std::fixed << std::setprecision(6);
    }
    if (autoStart) {
        start();
    }
}

CsvWriter::~CsvWriter() {
    stop();
}

void CsvWriter::start() {
    if (m_running.load()) return;
    if (!m_file.is_open()) return;
    m_running.store(true);
    m_worker = std::thread(&CsvWriter::workerLoop, this);
}

void CsvWriter::stop() {
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false)) return;
    if (m_reader) {
        m_reader->stop();
    }
    if (m_worker.joinable()) {
        m_worker.join();
    }
    if (m_file.is_open()) {
        m_file.flush();
    }
}

void CsvWriter::finish() {
    if (!m_running.load()) {
        return;
    }
    if (m_reader) {
        m_reader->finish();
    }
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_running.store(false);
    if (m_file.is_open()) {
        m_file.flush();
    }
}

bool CsvWriter::isRunning() const {
    return m_running.load();
}

void CsvWriter::workerLoop() {
    const auto& attrs = m_reader->getRule().getAttributes();

    while (m_running.load()) {
        Record rec = m_reader->nextRecord();

        if (!rec.isValid()) {
            break;
        }

        for (size_t i = 0; i < attrs.size(); ++i) {
            writeValue(m_file, rec, attrs[i]);
            if (i < attrs.size() - 1) m_file << m_delimiter;
        }
        m_file << "\n";
    }
}

void CsvWriter::writeHeader() {
    if (!m_file.is_open()) return;
    if (m_file.tellp() != 0) return;

    const auto& attrs = m_reader->getRule().getAttributes();
    for (size_t i = 0; i < attrs.size(); ++i) {
        m_file << attrs[i].name << (i < attrs.size() - 1 ? m_delimiter : "");
    }
    m_file << "\n";
}

void CsvWriter::writeValue(std::ofstream& file, const Record& rec, const PAttr& attr) {
    switch (attr.type) {
    case DataType::dtBool:   file << (rec.getBool(attr.id) ? "1" : "0"); break;
    case DataType::dtChar:
        if (attr.count > 1) file << "\"" << rec.getCharPtr(attr.id) << "\"";
        else file << rec.getChar(attr.id);
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
        std::string newFilename = createSuffixedFilename(m_filename);
        m_filename = newFilename;
        m_file.open(m_filename, std::ios::out);
        m_file << expectedHeader << "\n";
        m_file.flush();
    }
}

std::string CsvWriter::generateHeader() const {
    std::stringstream ss;
    const auto& attrs = m_reader->getRule().getAttributes();
    for (size_t i = 0; i < attrs.size(); ++i) {
        ss << attrs[i].name << (i < attrs.size() - 1 ? "," : "");
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
