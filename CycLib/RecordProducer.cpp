// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "RecordProducer.h"

namespace cyc {

RecordProducer::RecordProducer(size_t bufferCapacity, size_t writerBatchSize)
    : m_bufferCapacity(bufferCapacity)
    , m_writerBatchSize(writerBatchSize)
    , m_running(false)
    , m_isInitialized(false)
{
}

RecordProducer::~RecordProducer() {
    stop();
}

void RecordProducer::initialize() {
    // Fast path: lock-free check using acquire semantics
    if (m_isInitialized.load(std::memory_order_acquire)) {
        return;
    }

    // Slow path: lock and double-check
    std::lock_guard<std::mutex> lock(m_initMtx);
    if (m_isInitialized.load(std::memory_order_relaxed)) {
        return;
    }

    RecRule rule = defineRule();
    if (rule.getAttributes().empty()) {
        return;
    }

    m_buffer = std::make_shared<RecBuffer>(rule, m_bufferCapacity);
    m_writer = std::make_unique<RecordWriter>(m_buffer, m_writerBatchSize, true);

    // Publish the initialized state safely
    m_isInitialized.store(true, std::memory_order_release);
}

std::shared_ptr<RecBuffer> RecordProducer::getBuffer() {
    initialize();
    return m_buffer;
}

RecordWriter& RecordProducer::getWriter() {
    initialize();
    return *m_writer;
}

void RecordProducer::start() {
    if (m_running.load(std::memory_order_acquire)) return;

    initialize();
    if (!m_buffer || !m_writer) return;

    m_running.store(true, std::memory_order_release);
    m_worker = std::thread(&RecordProducer::workerLoop, this);
}

void RecordProducer::stop() {
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false)) return;

    if (m_writer) {
        m_writer->flush();
    }

    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void RecordProducer::join() {
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

bool RecordProducer::isRunning() const {
    return m_running.load(std::memory_order_acquire);
}

void RecordProducer::workerLoop() {
    onProduceStart();

    while (m_running.load(std::memory_order_relaxed)) {
        Record rec = m_writer->nextRecord();
        if (!produceStep(rec)) {
            break;
        }
        m_writer->commitRecord();
    }

    m_writer->flush();
    m_running.store(false, std::memory_order_release);

    onProduceStop();
}

// --- BatchRecordProducer Implementation ---

void BatchRecordProducer::workerLoop() {
    onProduceStart();

    while (m_running.load(std::memory_order_relaxed)) {
        auto batch = m_writer->nextBatch(m_writerBatchSize, true);

        if (!batch.isValid()) {
            break;
        }

        size_t written = produceBatch(batch);
        m_writer->commitBatch(written);

        if (written == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    m_writer->flush();
    m_running.store(false, std::memory_order_release);

    onProduceStop();
}

} // namespace cyc
