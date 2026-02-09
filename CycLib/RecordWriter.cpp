// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "RecordWriter.h"
#include "PReg.h"
#include <algorithm>

namespace cyc {

AsyncRecordWriter::AsyncRecordWriter(std::shared_ptr<RecBuffer> target, size_t batchCapacity)
    : m_target(target)
        , m_rule(m_target->getRule())       // Get rule from RecBuffer
        , m_recSize(m_target->getRecSize()) // Get size from RecBuffer
    , m_capacity(batchCapacity)
    , m_earlyThreshold(std::max<size_t>(1, batchCapacity / 5))
    , m_currentIdx(0)
    , m_bgCount(0)
    , m_running(true)
    , m_hasWork(false)
{
    m_bufferA.resize(m_capacity * m_recSize);
    m_bufferB.resize(m_capacity * m_recSize);
    m_activeBuf = &m_bufferA;
    m_bgBuf = &m_bufferB;

    m_worker = std::thread(&AsyncRecordWriter::workerLoop, this);
}

AsyncRecordWriter::~AsyncRecordWriter() {
    stop();
}

Record AsyncRecordWriter::nextRecord() {
    if (m_currentIdx >= m_capacity) {
        swapBuffers(true);
    } else if (m_currentIdx >= m_earlyThreshold) {
        swapBuffers(false);
    }
    uint8_t* ptr = m_activeBuf->data() + (m_currentIdx * m_recSize);
    return Record(m_rule, ptr);
}

void AsyncRecordWriter::commitRecord() {
    Record prevRec = nextRecord();
    auto TSId = PReg::getID("TimeStamp");
    if(prevRec.getDouble(TSId) == 0) {
        prevRec.setDouble(TSId, get_current_epoch_time());
    }
    m_currentIdx++;
}

void AsyncRecordWriter::flush() {
    if (m_currentIdx > 0) {
        swapBuffers(true);
    }
}

void AsyncRecordWriter::stop() {
    if (!m_running) return;
    flush();
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_running = false;
        m_hasWork = true;
    }
    m_cv.notify_one();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

bool AsyncRecordWriter::swapBuffers(bool blocking) {
    std::unique_lock<std::mutex> lock(m_mtx, std::defer_lock);

    if (blocking) {
        lock.lock();
        while (m_hasWork && m_running) {
            m_cv_done.wait(lock);
        }
    } else {
        if (!lock.try_lock()) return false;

        if (m_hasWork) return false;
    }

    m_bgCount = m_currentIdx;
    std::swap(m_activeBuf, m_bgBuf);
    m_currentIdx = 0;

    m_hasWork = true;
    lock.unlock();

    m_cv.notify_one();
    return true;
}

void AsyncRecordWriter::workerLoop() {
    while (true) {
        size_t countToFlush = 0;
        std::vector<uint8_t>* bufToFlush = nullptr;

        {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_cv.wait(lock, [this] { return m_hasWork; });

            if (!m_running && m_bgCount == 0) return;

            bufToFlush = m_bgBuf;
            countToFlush = m_bgCount;
        }

        if (countToFlush > 0) {
            m_target->push(bufToFlush->data(), countToFlush);
        }

        {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_hasWork = false;
            m_bgCount = 0;
        }
        m_cv_done.notify_one();
    }
}

} // namespace cyc
