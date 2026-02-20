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
    if (m_running.load(std::memory_order_acquire)) return;

    m_running.store(true, std::memory_order_release);
    m_worker = std::thread(&RecordConsumer::workerLoop, this);
}

void RecordConsumer::stop() {
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false)) return;

    if (m_reader) {
        m_reader->stop();
    }

    if (m_worker.joinable()) {
        if (std::this_thread::get_id() != m_worker.get_id()) {
            m_worker.join();
        } else {
            m_worker.detach();
        }
    }
}

void RecordConsumer::finish() {
    if (!m_running.load(std::memory_order_acquire)) {
        return;
    }

    if (m_reader) {
        m_reader->finish();
    }

    if (m_worker.joinable()) {
        if (std::this_thread::get_id() != m_worker.get_id()) {
            m_worker.join();
        } else {
            m_worker.detach();
        }
    }
    m_running.store(false, std::memory_order_release);
}

bool RecordConsumer::isRunning() const {
    return m_running.load(std::memory_order_acquire);
}

const RecordReader& RecordConsumer::getReader() const {
    return *m_reader;
}

void RecordConsumer::workerLoop() {
    onConsumeStart();

    while (m_running.load(std::memory_order_relaxed)) {
        Record rec = m_reader->nextRecord();

        if (!rec.isValid()) {
            break; // Reader was stopped or finished
        }

        consumeRecord(rec);
    }

    m_running.store(false, std::memory_order_release);
    onConsumeStop();
}

// --- BatchRecordConsumer Implementation ---

void BatchRecordConsumer::workerLoop() {
    onConsumeStart();

    while (m_running.load(std::memory_order_relaxed)) {
        // Fetch up to the entire internal capacity defined in RecordReader
        auto batch = m_reader->nextBatch(SIZE_MAX, true);

        if (!batch.isValid()) {
            break; // Reader was stopped or finished
        }

        consumeBatch(batch);
    }

    m_running.store(false, std::memory_order_release);
    onConsumeStop();
}

} // namespace cyc
