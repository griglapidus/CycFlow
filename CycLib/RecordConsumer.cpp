// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "RecordConsumer.h"

namespace cyc {

RecordConsumer::RecordConsumer(std::shared_ptr<RecBuffer> buffer, size_t readerBatchSize)
    : m_running(false)
{
    m_reader = std::make_unique<RecordReader>(buffer, readerBatchSize);
}

RecordConsumer::~RecordConsumer() {
    stop();
}

void RecordConsumer::start() {
    if (m_running.load()) return;
    m_running.store(true);
    m_worker = std::thread(&RecordConsumer::workerLoop, this);
}

void RecordConsumer::stop() {
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false)) return;

    if (m_reader) {
        m_reader->stop();
    }
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void RecordConsumer::finish() {
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
}

bool RecordConsumer::isRunning() const {
    return m_running.load();
}

const RecordReader& RecordConsumer::getReader() const {
    return *m_reader;
}

void RecordConsumer::workerLoop() {
    onConsumeStart();

    while (m_running.load()) {
        Record rec = m_reader->nextRecord();

        if (!rec.isValid()) {
            break;
        }

        consumeRecord(rec);
    }

    onConsumeStop();
}

} // namespace cyc
