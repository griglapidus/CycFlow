// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "CbfWriter.h"
#include <iostream>

namespace cyc {

CbfWriter::CbfWriter(const std::string& filename,
                     std::shared_ptr<RecBuffer> buffer,
                     bool autoStart,
                     size_t batchSize)
    : BatchRecordConsumer(buffer, batchSize)
    , m_filename(filename)
    , m_alias("Default")
{
    if (autoStart) {
        start();
    }
}

CbfWriter::~CbfWriter() {
    stop();
}

void CbfWriter::setAlias(const std::string& alias) {
    m_alias = alias;
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
    if (batch.count == 0) return;

    size_t bytesToWrite = batch.count * batch.recordSize;

    if (!m_cbfFile.writeBytes(batch.data, bytesToWrite)) {
        std::cerr << "CbfWriter: Failed to write data block of size " << bytesToWrite << std::endl;
    }
}

void CbfWriter::onConsumeStop() {
    m_cbfFile.close(); // Implicitly calls endDataSection()
}

} // namespace cyc
