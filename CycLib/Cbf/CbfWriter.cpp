// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "CbfWriter.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace cyc {

CbfWriter::CbfWriter() = default;

CbfWriter::CbfWriter(const std::string& filename,
                     std::shared_ptr<RecBuffer> buffer,
                     bool autoStart,
                     size_t batchSize,
                     bool addTimestampSuffix,
                     size_t maxRecords)
    : CbfWriter()
{
    init(filename, buffer, autoStart, batchSize, addTimestampSuffix, maxRecords);
}

CbfWriter::~CbfWriter() {
    stop();
}

std::string CbfWriter::createSuffixedFilename(const std::string &originalName) const {
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

void CbfWriter::setAlias(const std::string& alias) {
    m_alias = alias;
}

void CbfWriter::restart() {
    m_restartRequested.store(true, std::memory_order_release);
}

void CbfWriter::rotateFile() {
    m_cbfFile.close();
    m_filename = m_addTimestampSuffix ? createSuffixedFilename(m_baseFilename) : m_baseFilename;

    if (!m_cbfFile.open(m_filename, CbfMode::Write)) {
        std::cerr << "CbfWriter: Failed to open file " << m_filename << std::endl;
        return;
    }
    m_cbfFile.setAlias(m_alias);

    const RecRule& rule = getReader().getRule();
    if (!m_cbfFile.writeHeader(rule)) {
        std::cerr << "CbfWriter: Failed to write RecRule header" << std::endl;
        m_cbfFile.close();
        return;
    }
    if (!m_cbfFile.beginDataSection()) {
        std::cerr << "CbfWriter: Failed to begin data section" << std::endl;
        m_cbfFile.close();
        return;
    }
    m_recordCount = 0;
}

void CbfWriter::onConsumeStart() {
    if (!m_cbfFile.open(m_filename, CbfMode::Write)) {
        std::cerr << "CbfWriter: Failed to open file " << m_filename << std::endl;
        return;
    }

    m_cbfFile.setAlias(m_alias);

    const RecRule& rule = getReader().getRule();

    if (!m_cbfFile.writeHeader(rule)) {
        std::cerr << "CbfWriter: Failed to write RecRule header" << std::endl;
        m_cbfFile.close();
        return;
    }

    if (!m_cbfFile.beginDataSection()) {
        std::cerr << "CbfWriter: Failed to begin data section" << std::endl;
        m_cbfFile.close();
        return;
    }
}

void CbfWriter::consumeBatch(const RecordReader::RecordBatch& batch) {
    if (m_restartRequested.exchange(false, std::memory_order_acq_rel)) {
        rotateFile();
    }

    if (batch.count == 0 || !m_cbfFile.isOpen()) return;

    size_t bytesToWrite = batch.count * batch.recordSize;
    if (!m_cbfFile.writeBytes(batch.data, bytesToWrite)) {
        std::cerr << "CbfWriter: Failed to write data block of size " << bytesToWrite << std::endl;
    }

    if (m_maxRecords > 0) {
        m_recordCount += batch.count;
        if (m_recordCount >= m_maxRecords) {
            rotateFile();
        }
    }
}

void CbfWriter::onConsumeStop() {
    m_cbfFile.close(); // Implicitly calls endDataSection()
}

} // namespace cyc
