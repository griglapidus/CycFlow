// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "RecordProducer.h"
#include <stdexcept>

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
    std::lock_guard<std::mutex> lock(m_initMtx);
    if (m_isInitialized) return;
    RecRule rule = defineRule();
    if(rule.getAttributes().empty()) return;
    m_buffer = std::make_shared<RecBuffer>(rule, m_bufferCapacity);
    m_writer = std::make_unique<RecordWriter>(m_buffer, m_writerBatchSize, true);

    m_isInitialized = true;
}

std::shared_ptr<RecBuffer> RecordProducer::getBuffer() {
    if (!m_isInitialized) {
        initialize();
    }
    return m_buffer;
}

RecordWriter& RecordProducer::getWriter() {
    if (!m_isInitialized) {
        initialize();
    }
    return *m_writer;
}

void RecordProducer::start() {
    if (m_running.load()) return;

    // Гарантируем, что всё создано перед запуском потока
    initialize();

    if (!m_buffer || !m_writer) return;

    m_running.store(true);
    m_worker = std::thread(&RecordProducer::workerLoop, this);
}

void RecordProducer::stop() {
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false)) return;

    // Сбрасываем данные, только если writer был создан
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
    return m_running.load();
}

void RecordProducer::workerLoop() {
    onProduceStart();

    while (m_running.load()) {
        Record rec = m_writer->nextRecord();
        if (!produceStep(rec)) {
            break;
        }
        m_writer->commitRecord();
    }
    m_writer->flush();
    m_running.store(false);

    onProduceStop();
}

} // namespace cyc
