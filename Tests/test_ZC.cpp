// test_ZC.cpp
// SPDX-License-Identifier: MIT
//
// Integration and performance tests for RecordWriterZC and RecordReaderZC.
// Each test mirrors a counterpart in test_Core.cpp; results are printed with a
// matching label so the two files can be compared side-by-side.

#include <gtest/gtest.h>
#include <vector>
#include <array>
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <iomanip>

#include "Core/PReg.h"
#include "Core/PAttr.h"
#include "Core/RecRule.h"
#include "Core/Record.h"
#include "Core/RecBuffer.h"
#include "RecordWriter.h"
#include "RecordWriterZC.h"
#include "RecordReader.h"
#include "RecordReaderZC.h"

using namespace cyc;

// =============================================================================
// Helpers
// =============================================================================

// Runs the standard write-then-read integration flow with any
// WriterT / ReaderT combination. Mirrors AsyncIntegrationTest::WriteReadFlow.
template<typename WriterT, typename ReaderT>
void runWriteReadFlow(int idVal, const RecRule& rule,
                      size_t writerBatch, size_t readerBatch,
                      int totalRecords = 100000)
{
    auto buffer = std::make_shared<RecBuffer>(rule, 50000);
    WriterT writer(buffer, writerBatch);
    ReaderT reader(buffer, readerBatch);

    std::thread producer([&]() {
        for (int i = 0; i < totalRecords; ++i) {
            Record r = writer.nextRecord();
            r.setInt32(idVal, i);
            writer.commitRecord();
        }
        writer.flush();
    });

    std::vector<int> received(totalRecords, 0);
    for (int i = 0; i < totalRecords; ++i) {
        Record r = reader.nextRecord();
        received[i] = r.getInt32(idVal);
    }

    producer.join();

    for (int i = 0; i < totalRecords; ++i) {
        ASSERT_EQ(received[i], i) << "at record index " << i;
    }
}

// Sequential write-chunk / read-chunk throughput benchmark.
// Mirrors RecordPerformanceTest::FieldReadWriteThroughput.
template<typename WriterT, typename ReaderT>
void runFieldReadWriteThroughput(bool align, const char* label)
{
    std::vector<PAttr> attrs;
    attrs.emplace_back("ZCPerfI8",  DataType::dtInt8);
    attrs.emplace_back("ZCPerfU8",  DataType::dtUInt8);
    attrs.emplace_back("ZCPerfI16", DataType::dtInt16);
    attrs.emplace_back("ZCPerfU32", DataType::dtInt32,  2);
    attrs.emplace_back("ZCPerfI32", DataType::dtUInt32);
    attrs.emplace_back("ZCPerfI64", DataType::dtInt64);
    attrs.emplace_back("ZCPerfFlt", DataType::dtFloat,  2);
    attrs.emplace_back("ZCPerfDbl", DataType::dtDouble, 2);
    RecRule rule(attrs, align);

    const int idI8  = PReg::getID("ZCPerfI8");
    const int idU8  = PReg::getID("ZCPerfU8");
    const int idI16 = PReg::getID("ZCPerfI16");
    const int idI32 = PReg::getID("ZCPerfI32");
    const int idU32 = PReg::getID("ZCPerfUI32");
    const int idI64 = PReg::getID("ZCPerfI64");
    const int idFlt = PReg::getID("ZCPerfFlt");
    const int idDbl = PReg::getID("ZCPerfDbl");

    const size_t recSize      = rule.getRecSize();
    const size_t kRecordCount = 128000;
    const size_t kChunkSize   = 4000;

    auto buffer = std::make_shared<RecBuffer>(rule, kRecordCount);
    WriterT recWriter(buffer, kChunkSize);
    ReaderT recReader(buffer, kChunkSize);

    using clock = std::chrono::steady_clock;
    const auto budget      = std::chrono::milliseconds(1000);
    const size_t fieldsPerCycle = attrs.size();

    volatile int64_t sink = 0;
    uint64_t totalRecords = 0;
    clock::duration writeElapsed{0};
    clock::duration readElapsed{0};
    uint64_t counter = 0;

    const auto loopDeadline = clock::now() + budget;
    while (clock::now() < loopDeadline) {
        // --- Write one chunk via batch API ---
        const auto writeBegin = clock::now();
        size_t toWrite = kChunkSize;
        while (toWrite > 0) {
            auto batch = recWriter.nextBatch(toWrite, /*wait=*/true);
            if (!batch.isValid()) break;
            for (size_t i = 0; i < batch.capacity; ++i) {
                Record rec(batch.rule, batch.data + i * batch.recordSize);
                rec.setInt8  (idI8,  static_cast<int8_t  >(counter));
                rec.setInt8  (idU8,  static_cast<uint8_t >(counter));
                rec.setInt16 (idI16, static_cast<int16_t >(counter));
                rec.setInt32 (idI32, static_cast<int32_t >(counter));
                rec.setInt32 (idU32, static_cast<uint32_t>(counter), 0);
                rec.setInt32 (idU32, static_cast<uint32_t>(counter), 1);
                rec.setInt64 (idI64, static_cast<int64_t >(counter));
                rec.setFloat (idFlt, static_cast<float   >(counter), 0);
                rec.setFloat (idFlt, static_cast<float   >(counter), 1);
                rec.setDouble(idDbl, static_cast<double  >(counter), 0);
                rec.setDouble(idDbl, static_cast<double  >(counter), 1);
                ++counter;
            }
            recWriter.commitBatch(batch.capacity);
            toWrite -= batch.capacity;
        }
        recWriter.flush();
        writeElapsed += clock::now() - writeBegin;

        // --- Read one chunk via batch API ---
        const auto readBegin = clock::now();
        size_t toRead = kChunkSize;
        while (toRead > 0) {
            auto batch = recReader.nextBatch(toRead, /*wait=*/true);
            if (!batch.isValid()) break;
            for (size_t i = 0; i < batch.count; ++i) {
                Record rec(batch.rule,
                           const_cast<uint8_t*>(batch.data + i * batch.recordSize));
                sink += rec.getInt8   (idI8);
                sink += rec.getUInt8  (idU8);
                sink += rec.getInt16  (idI16);
                sink += rec.getInt32  (idI32);
                sink += rec.getUInt32 (idU32, 1);
                sink += rec.getUInt32 (idU32, 2);
                sink += rec.getInt64  (idI64);
                sink += static_cast<int64_t>(rec.getFloat (idFlt), 1);
                sink += static_cast<int64_t>(rec.getFloat (idFlt), 2);
                sink += static_cast<int64_t>(rec.getDouble(idDbl), 1);
                sink += static_cast<int64_t>(rec.getDouble(idDbl), 2);
            }
            toRead -= batch.count;
        }
        readElapsed += clock::now() - readBegin;

        totalRecords += kChunkSize;
    }

    const auto writeMs = std::chrono::duration_cast<std::chrono::milliseconds>(writeElapsed).count();
    const auto readMs  = std::chrono::duration_cast<std::chrono::milliseconds>(readElapsed).count();
    const uint64_t writesPerSec = writeMs > 0 ? (totalRecords * 1000ULL) / static_cast<uint64_t>(writeMs) : 0;
    const uint64_t readsPerSec  = readMs  > 0 ? (totalRecords * 1000ULL) / static_cast<uint64_t>(readMs)  : 0;
    const uint64_t totalBytes   = totalRecords * static_cast<uint64_t>(recSize);
    const double   writeMiBps   = writeMs > 0 ? (static_cast<double>(totalBytes) / 1048576.0) * 1000.0 / static_cast<double>(writeMs) : 0.0;
    const double   readMiBps    = readMs  > 0 ? (static_cast<double>(totalBytes) / 1048576.0) * 1000.0 / static_cast<double>(readMs)  : 0.0;

    std::cout << "[Perf/" << label << "] budget: " << budget.count()
              << " ms, fields/cycle: " << fieldsPerCycle
              << ", recSize: " << recSize << " B"
              << ", chunk: " << kChunkSize
              << ", bufRecs: " << kRecordCount << "\n";
    std::cout << "[Perf/" << label << "] Total: " << totalRecords
              << " (" << totalBytes / 1048576 << " MiB)\n";
    std::cout << "[Perf/" << label << "] Write: " << writeMs << " ms"
              << "  (" << writesPerSec << " rec/s, "
              << writesPerSec * fieldsPerCycle << " fld-w/s, "
              << writeMiBps << " MiB/s)\n";
    std::cout << "[Perf/" << label << "] Read : " << readMs  << " ms"
              << "  (" << readsPerSec  << " rec/s, "
              << readsPerSec  * fieldsPerCycle << " fld-r/s, "
              << readMiBps << " MiB/s)\n";
    std::cout << "[Perf/" << label << "] sink (ignore): " << sink << '\n';

    EXPECT_GT(totalRecords, 0u);
}

// Concurrent writer + reader throughput benchmark.
// Mirrors RecordPerformanceTest::ConcurrentFieldReadWriteThroughput.
template<typename WriterT, typename ReaderT>
void runConcurrentFieldReadWriteThroughput(bool align, const char* label)
{
    std::vector<PAttr> attrs;
    attrs.emplace_back("ZCConcI8",  DataType::dtInt8);
    attrs.emplace_back("ZCConcU8",  DataType::dtUInt8);
    attrs.emplace_back("ZCConcI16", DataType::dtInt16);
    attrs.emplace_back("ZCConcU32", DataType::dtInt32,  2);
    attrs.emplace_back("ZCConcI32", DataType::dtUInt32);
    attrs.emplace_back("ZCConcI64", DataType::dtInt64);
    attrs.emplace_back("ZCConcFlt", DataType::dtFloat,  2);
    attrs.emplace_back("ZCConcDbl", DataType::dtDouble, 2);
    RecRule rule(attrs, align);

    const int idI8  = PReg::getID("ZCConcI8");
    const int idU8  = PReg::getID("ZCConcU8");
    const int idI16 = PReg::getID("ZCConcI16");
    const int idI32 = PReg::getID("ZCConcI32");
    const int idU32 = PReg::getID("ZCConcUI32");
    const int idI64 = PReg::getID("ZCConcI64");
    const int idFlt = PReg::getID("ZCConcFlt");
    const int idDbl = PReg::getID("ZCConcDbl");

    const size_t recSize      = rule.getRecSize();
    const size_t kRecordCount = 128000;
    const size_t kChunkSize   = 4000;

    auto buffer = std::make_shared<RecBuffer>(rule, kRecordCount);
    WriterT recWriter(buffer, kChunkSize);
    ReaderT recReader(buffer, kChunkSize);

    using clock = std::chrono::steady_clock;
    const auto budget = std::chrono::milliseconds(1000);
    const size_t fieldsPerCycle = attrs.size();

    std::atomic<bool>     stopFlag{false};
    std::atomic<uint64_t> writtenRecords{0};
    std::atomic<uint64_t> readRecords{0};
    std::atomic<int64_t>  sinkTotal{0};

    std::thread writerThread([&]() {
        uint64_t counter = 0;
        while (!stopFlag.load(std::memory_order_relaxed)) {
            auto batch = recWriter.nextBatch(kChunkSize, /*wait=*/true);
            if (!batch.isValid()) break;
            for (size_t i = 0; i < batch.capacity; ++i) {
                Record rec(batch.rule, batch.data + i * batch.recordSize);
                rec.setInt8  (idI8,  static_cast<int8_t  >(counter));
                rec.setInt8  (idU8,  static_cast<uint8_t >(counter));
                rec.setInt16 (idI16, static_cast<int16_t >(counter));
                rec.setInt32 (idI32, static_cast<int32_t >(counter));
                rec.setInt32 (idU32, static_cast<uint32_t>(counter), 0);
                rec.setInt32 (idU32, static_cast<uint32_t>(counter), 1);
                rec.setInt64 (idI64, static_cast<int64_t >(counter));
                rec.setFloat (idFlt, static_cast<float   >(counter), 0);
                rec.setFloat (idFlt, static_cast<float   >(counter), 1);
                rec.setDouble(idDbl, static_cast<double  >(counter), 0);
                rec.setDouble(idDbl, static_cast<double  >(counter), 1);
                ++counter;
            }
            recWriter.commitBatch(batch.capacity);
        }
        recWriter.flush();
        writtenRecords.store(counter);
    });

    std::thread readerThread([&]() {
        int64_t  sink = 0;
        uint64_t readCounter = 0;
        while (!stopFlag.load(std::memory_order_relaxed)) {
            auto batch = recReader.nextBatch(kChunkSize, /*wait=*/false);
            if (!batch.isValid()) {
                std::this_thread::yield();
                continue;
            }
            for (size_t i = 0; i < batch.count; ++i) {
                Record rec(batch.rule,
                           const_cast<uint8_t*>(batch.data + i * batch.recordSize));
                sink += rec.getInt8   (idI8);
                sink += rec.getUInt8  (idU8);
                sink += rec.getInt16  (idI16);
                sink += rec.getInt32  (idI32);
                sink += rec.getUInt32 (idU32, 1);
                sink += rec.getUInt32 (idU32, 2);
                sink += rec.getInt64  (idI64);
                sink += static_cast<int64_t>(rec.getFloat (idFlt), 1);
                sink += static_cast<int64_t>(rec.getFloat (idFlt), 2);
                sink += static_cast<int64_t>(rec.getDouble(idDbl), 1);
                sink += static_cast<int64_t>(rec.getDouble(idDbl), 2);
            }
            readCounter += batch.count;
        }
        readRecords.store(readCounter);
        sinkTotal.store(sink);
    });

    const auto runStart = clock::now();
    std::this_thread::sleep_for(budget);
    stopFlag.store(true, std::memory_order_relaxed);

    writerThread.join();
    readerThread.join();
    const auto elapsed = clock::now() - runStart;

    const uint64_t written     = writtenRecords.load();
    const uint64_t readCnt     = readRecords.load();
    const auto     elapsedMs   = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    const uint64_t writesPerSec = elapsedMs > 0 ? (written * 1000ULL) / static_cast<uint64_t>(elapsedMs) : 0;
    const uint64_t readsPerSec  = elapsedMs > 0 ? (readCnt * 1000ULL) / static_cast<uint64_t>(elapsedMs) : 0;
    const double   readRatio    = written > 0 ? static_cast<double>(readCnt) / static_cast<double>(written) : 0.0;
    const uint64_t writtenBytes = written * static_cast<uint64_t>(recSize);
    const uint64_t readBytes    = readCnt * static_cast<uint64_t>(recSize);
    const double   writeMiBps   = elapsedMs > 0 ? (static_cast<double>(writtenBytes) / 1048576.0) * 1000.0 / static_cast<double>(elapsedMs) : 0.0;
    const double   readMiBps    = elapsedMs > 0 ? (static_cast<double>(readBytes)    / 1048576.0) * 1000.0 / static_cast<double>(elapsedMs) : 0.0;

    std::cout << "[PerfConc/" << label << "] budget: " << budget.count()
              << " ms, elapsed: " << elapsedMs
              << " ms, fields/cycle: " << fieldsPerCycle
              << ", recSize: " << recSize << " B"
              << ", chunk: " << kChunkSize
              << ", bufRecs: " << kRecordCount << "\n";
    std::cout << "[PerfConc/" << label << "] Written: " << written
              << " (" << writtenBytes / 1048576 << " MiB)"
              << "  (" << writesPerSec << " rec/s, "
              << writesPerSec * fieldsPerCycle << " fld-w/s, "
              << writeMiBps << " MiB/s)\n";
    std::cout << "[PerfConc/" << label << "] Read   : " << readCnt
              << " (" << readBytes / 1048576 << " MiB)"
              << "  (" << readsPerSec << " rec/s, "
              << readsPerSec * fieldsPerCycle << " fld-r/s, "
              << readMiBps << " MiB/s)"
              << ", read/written = " << readRatio << "\n";
    std::cout << "[PerfConc/" << label << "] sink (ignore): " << sinkTotal.load() << '\n';

    EXPECT_GT(written, 0u);
    EXPECT_GT(readCnt, 0u);
}

// =============================================================================
// Basic integration tests  (mirrors AsyncIntegrationTest::WriteReadFlow)
// =============================================================================

class ZCIntegrationTest : public ::testing::TestWithParam<std::tuple<double, double>> {};

INSTANTIATE_TEST_SUITE_P(
    StandardSuite,
    ZCIntegrationTest,
    ::testing::Combine(
        ::testing::Values(0.2, 1.0),
        ::testing::Values(0.2, 1.0)
    )
);

TEST_P(ZCIntegrationTest, Writer_ReaderZC) {
    int idVal = PReg::getID("ZCVal1");
    RecRule rule({ PAttr("ZCVal1", DataType::dtInt32) });
    const double p1 = std::get<0>(GetParam());
    const double p2 = std::get<1>(GetParam());
    runWriteReadFlow<RecordWriter, RecordReaderZC>(
        idVal, rule,
        static_cast<size_t>(10000 * p1),
        static_cast<size_t>(10000 * p2));
}

TEST_P(ZCIntegrationTest, WriterZC_Reader) {
    int idVal = PReg::getID("ZCVal2");
    RecRule rule({ PAttr("ZCVal2", DataType::dtInt32) });
    const double p1 = std::get<0>(GetParam());
    const double p2 = std::get<1>(GetParam());
    runWriteReadFlow<RecordWriterZC, RecordReader>(
        idVal, rule,
        static_cast<size_t>(10000 * p1),
        static_cast<size_t>(10000 * p2));
}

TEST_P(ZCIntegrationTest, WriterZC_ReaderZC) {
    int idVal = PReg::getID("ZCVal3");
    RecRule rule({ PAttr("ZCVal3", DataType::dtInt32) });
    const double p1 = std::get<0>(GetParam());
    const double p2 = std::get<1>(GetParam());
    runWriteReadFlow<RecordWriterZC, RecordReaderZC>(
        idVal, rule,
        static_cast<size_t>(10000 * p1),
        static_cast<size_t>(10000 * p2));
}

// =============================================================================
// Sequential throughput  (mirrors RecordPerformanceTest::FieldReadWriteThroughput)
// =============================================================================

class ZCPerformanceTest : public ::testing::TestWithParam<bool> {};

INSTANTIATE_TEST_SUITE_P(
    AlignModes,
    ZCPerformanceTest,
    ::testing::Values(false, true),
    [](const ::testing::TestParamInfo<bool>& info) {
        return info.param ? "Aligned" : "Packed";
    });

TEST_P(ZCPerformanceTest, Writer_ReaderZC_FieldReadWriteThroughput) {
    const bool align = GetParam();
    const char* label = align ? "Writer+ReaderZC Aligned" : "Writer+ReaderZC Packed";
    runFieldReadWriteThroughput<RecordWriter, RecordReaderZC>(align, label);
}

TEST_P(ZCPerformanceTest, WriterZC_Reader_FieldReadWriteThroughput) {
    const bool align = GetParam();
    const char* label = align ? "WriterZC+Reader Aligned" : "WriterZC+Reader Packed";
    runFieldReadWriteThroughput<RecordWriterZC, RecordReader>(align, label);
}

TEST_P(ZCPerformanceTest, WriterZC_ReaderZC_FieldReadWriteThroughput) {
    const bool align = GetParam();
    const char* label = align ? "WriterZC+ReaderZC Aligned" : "WriterZC+ReaderZC Packed";
    runFieldReadWriteThroughput<RecordWriterZC, RecordReaderZC>(align, label);
}

// =============================================================================
// Concurrent throughput  (mirrors RecordPerformanceTest::ConcurrentFieldReadWriteThroughput)
// =============================================================================

TEST_P(ZCPerformanceTest, Writer_ReaderZC_ConcurrentThroughput) {
    const bool align = GetParam();
    const char* label = align ? "Writer+ReaderZC Aligned" : "Writer+ReaderZC Packed";
    runConcurrentFieldReadWriteThroughput<RecordWriter, RecordReaderZC>(align, label);
}

TEST_P(ZCPerformanceTest, WriterZC_Reader_ConcurrentThroughput) {
    const bool align = GetParam();
    const char* label = align ? "WriterZC+Reader Aligned" : "WriterZC+Reader Packed";
    runConcurrentFieldReadWriteThroughput<RecordWriterZC, RecordReader>(align, label);
}

TEST_P(ZCPerformanceTest, WriterZC_ReaderZC_ConcurrentThroughput) {
    const bool align = GetParam();
    const char* label = align ? "WriterZC+ReaderZC Aligned" : "WriterZC+ReaderZC Packed";
    runConcurrentFieldReadWriteThroughput<RecordWriterZC, RecordReaderZC>(align, label);
}

// =============================================================================
// Batch-size sweep
//
// Measures concurrent throughput for a range of batch sizes using the BATCH
// API (nextBatch / commitBatch) on both writer and reader. The single-record
// tests above show that per-record push() cost dominates for WriterZC; this
// sweep finds the minimum batch size at which that cost is amortised.
//
// Tested batch sizes: 1, 4, 16, 64, 256, 1024, 4096
// Budget per measurement: 400 ms  → total per test ≈ 3 s
// =============================================================================

struct BatchSweepResult {
    size_t   batchSize;
    uint64_t writesPerSec;
    uint64_t readsPerSec;
    double   writeMiBps;
    double   readMiBps;
    double   readRatio;
};

// Runs one 400 ms concurrent-batch measurement and returns the metrics.
template<typename WriterT, typename ReaderT>
BatchSweepResult measureBatchPoint(
    size_t batchSize,
    const RecRule& rule, size_t recSize,
    int idI8,  int idU8,  int idI16,
    int idI32, int idU32, int idI64,
    int idFlt, int idDbl)
{
    // Use batchSize as the writer/reader capacity so nextBatch(batchSize)
    // always returns a full batch.
    const size_t capacity    = std::max(batchSize, size_t{1});
    // Ring buffer sized to hold 64 full batches. That is large enough to give
    // both threads room to run concurrently, but still small enough not to OOM.
    // blockOnFull=false: writer never blocks — required to avoid a deadlock
    // when the reader stops advancing its cursor at test shutdown.
    const size_t ringRecords = std::max(capacity * 64, size_t{4096});

    auto buffer = std::make_shared<RecBuffer>(rule, ringRecords);
    WriterT recWriter(buffer, capacity);
    ReaderT recReader(buffer, capacity);

    using clock = std::chrono::steady_clock;
    const auto budget = std::chrono::milliseconds(400);

    std::atomic<bool>     stopFlag{false};
    std::atomic<uint64_t> writtenRec{0};
    std::atomic<uint64_t> readRec{0};
    std::atomic<int64_t>  sinkAcc{0};

    std::thread writerThread([&]() {
        uint64_t counter = 0;
        while (!stopFlag.load(std::memory_order_relaxed)) {
            auto batch = recWriter.nextBatch(batchSize, /*wait=*/true);
            if (!batch.isValid()) break;

            for (size_t i = 0; i < batch.capacity; ++i) {
                Record rec(batch.rule, batch.data + i * batch.recordSize);
                rec.setInt8  (idI8,  static_cast<int8_t  >(counter));
                rec.setInt8  (idU8,  static_cast<uint8_t >(counter));
                rec.setInt16 (idI16, static_cast<int16_t >(counter));
                rec.setInt32 (idI32, static_cast<int32_t >(counter));
                rec.setInt32 (idU32, static_cast<uint32_t>(counter), 0);
                rec.setInt32 (idU32, static_cast<uint32_t>(counter), 1);
                rec.setInt64 (idI64, static_cast<int64_t >(counter));
                rec.setFloat (idFlt, static_cast<float   >(counter), 0);
                rec.setFloat (idFlt, static_cast<float   >(counter), 1);
                rec.setDouble(idDbl, static_cast<double  >(counter), 0);
                rec.setDouble(idDbl, static_cast<double  >(counter), 1);
                ++counter;
            }
            recWriter.commitBatch(batch.capacity);
        }
        recWriter.flush();
        writtenRec.store(counter);
    });

    std::thread readerThread([&]() {
        int64_t  sink = 0;
        uint64_t rCnt = 0;
        while (!stopFlag.load(std::memory_order_relaxed)) {
            auto batch = recReader.nextBatch(batchSize, /*wait=*/false);
            if (!batch.isValid()) { std::this_thread::yield(); continue; }

            for (size_t i = 0; i < batch.count; ++i) {
                Record rec(batch.rule,
                           const_cast<uint8_t*>(batch.data + i * batch.recordSize));
                sink += rec.getInt8   (idI8);
                sink += rec.getUInt8  (idU8);
                sink += rec.getInt16  (idI16);
                sink += rec.getInt32  (idI32);
                sink += rec.getUInt32 (idU32, 1);
                sink += rec.getUInt32 (idU32, 2);
                sink += rec.getInt64  (idI64);
                sink += static_cast<int64_t>(rec.getFloat (idFlt), 1);
                sink += static_cast<int64_t>(rec.getFloat (idFlt), 2);
                sink += static_cast<int64_t>(rec.getDouble(idDbl), 1);
                sink += static_cast<int64_t>(rec.getDouble(idDbl), 2);
            }
            rCnt += batch.count;
        }
        readRec.store(rCnt);
        sinkAcc.store(sink);
    });

    const auto t0 = clock::now();
    std::this_thread::sleep_for(budget);
    stopFlag.store(true, std::memory_order_relaxed);

    // Unblock any pending waitForSpace() in the writer before joining threads.
    // RecordWriterZC::commitBatch() can be stuck there when the ring buffer is
    // full and readers have stopped advancing their cursor. For RecordWriter
    // (buffered) stop() is a no-op — its destructor handles shutdown.
    recWriter.stop();

    readerThread.join();
    recReader.release();  // free any pinned ZC batch so writer flush can proceed
    writerThread.join();

    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock::now() - t0).count();

    const uint64_t w  = writtenRec.load();
    const uint64_t r  = readRec.load();
    const auto     ms = static_cast<uint64_t>(elapsedMs > 0 ? elapsedMs : 1);

    BatchSweepResult res;
    res.batchSize    = batchSize;
    res.writesPerSec = w * 1000ULL / ms;
    res.readsPerSec  = r * 1000ULL / ms;
    res.writeMiBps   = static_cast<double>(w * recSize) / 1048576.0 * 1000.0 / static_cast<double>(ms);
    res.readMiBps    = static_cast<double>(r * recSize) / 1048576.0 * 1000.0 / static_cast<double>(ms);
    res.readRatio    = w > 0 ? static_cast<double>(r) / static_cast<double>(w) : 0.0;

    // consume sink to prevent dead-store elimination
    (void)sinkAcc.load();
    return res;
}

// Runs the full batch-size sweep and prints a table.
template<typename WriterT, typename ReaderT>
void runBatchSweep(bool align, const char* combo)
{
    std::vector<PAttr> attrs;
    attrs.emplace_back("SwI8",  DataType::dtInt8);
    attrs.emplace_back("SwU8",  DataType::dtUInt8);
    attrs.emplace_back("SwI16", DataType::dtInt16);
    attrs.emplace_back("SwU32", DataType::dtInt32,  2);
    attrs.emplace_back("SwI32", DataType::dtUInt32);
    attrs.emplace_back("SwI64", DataType::dtInt64);
    attrs.emplace_back("SwFlt", DataType::dtFloat,  2);
    attrs.emplace_back("SwDbl", DataType::dtDouble, 2);
    RecRule rule(attrs, align);
    const size_t recSize = rule.getRecSize();

    const int idI8  = PReg::getID("SwI8");
    const int idU8  = PReg::getID("SwU8");
    const int idI16 = PReg::getID("SwI16");
    const int idI32 = PReg::getID("SwI32");
    const int idU32 = PReg::getID("SwUI32");
    const int idI64 = PReg::getID("SwI64");
    const int idFlt = PReg::getID("SwFlt");
    const int idDbl = PReg::getID("SwDbl");

    constexpr std::array<size_t, 7> kSizes{1, 4, 16, 64, 256, 1024, 4096};

    std::cout << "\n[BatchSweep/" << combo << "] recSize=" << recSize
              << " B  (400 ms / point)\n";
    std::cout << std::left
              << std::setw(8)  << "batch"
              << std::setw(14) << "write rec/s"
              << std::setw(12) << "write MiB/s"
              << std::setw(14) << "read  rec/s"
              << std::setw(12) << "read  MiB/s"
              << "read/written\n";
    std::cout << std::string(74, '-') << '\n';

    size_t prevWriteRec = 0;
    for (size_t bs : kSizes) {
        auto r = measureBatchPoint<WriterT, ReaderT>(
            bs, rule, recSize,
            idI8, idU8, idI16, idI32, idU32, idI64, idFlt, idDbl);

        const char* marker = "";
        // Mark the first batch size where write throughput levels off
        // (less than 20 % gain over previous point).
        if (prevWriteRec > 0 && r.writesPerSec > 0) {
            double gain = static_cast<double>(r.writesPerSec - prevWriteRec)
                        / static_cast<double>(prevWriteRec);
            if (gain < 0.20) marker = " ← plateau";
        }
        prevWriteRec = r.writesPerSec;

        std::cout << std::left
                  << std::setw(8)  << bs
                  << std::setw(14) << r.writesPerSec
                  << std::setw(12) << static_cast<long long>(r.writeMiBps)
                  << std::setw(14) << r.readsPerSec
                  << std::setw(12) << static_cast<long long>(r.readMiBps)
                  << std::fixed << std::setprecision(3) << r.readRatio
                  << marker << '\n';

        EXPECT_GT(r.writesPerSec, 0u);
        EXPECT_GT(r.readsPerSec,  0u);
    }
    std::cout << '\n';
}

// =============================================================================
// Batch-sweep test suite
// =============================================================================

class ZCBatchSweepTest : public ::testing::TestWithParam<bool> {};

INSTANTIATE_TEST_SUITE_P(
    AlignModes,
    ZCBatchSweepTest,
    ::testing::Values(false, true),
    [](const ::testing::TestParamInfo<bool>& info) {
        return info.param ? "Aligned" : "Packed";
    });

TEST_P(ZCBatchSweepTest, Writer_ReaderZC_BatchSweep) {
    const bool align = GetParam();
    runBatchSweep<RecordWriter, RecordReaderZC>(
        align, align ? "Writer+ReaderZC Aligned" : "Writer+ReaderZC Packed");
}

TEST_P(ZCBatchSweepTest, WriterZC_Reader_BatchSweep) {
    const bool align = GetParam();
    runBatchSweep<RecordWriterZC, RecordReader>(
        align, align ? "WriterZC+Reader Aligned" : "WriterZC+Reader Packed");
}

TEST_P(ZCBatchSweepTest, WriterZC_ReaderZC_BatchSweep) {
    const bool align = GetParam();
    runBatchSweep<RecordWriterZC, RecordReaderZC>(
        align, align ? "WriterZC+ReaderZC Aligned" : "WriterZC+ReaderZC Packed");
}

// =============================================================================
// Dual-writer / dual-reader stress test
//
// Two concurrent RecordWriter instances (buffered, double-buffered + worker
// thread) share one RecBuffer with two independent readers. RecordWriter is
// required here because the system does not support multiple simultaneous
// zero-copy (ZC) writers.
//
// Both readers independently consume every record written by both writers, so
// the buffer must serve two read cursors. The test runs for 3 seconds to expose
// race conditions and deadlocks that short tests may miss.
// =============================================================================

template<typename ReaderT>
void runDualWriterDualReader(bool align, const char* label)
{
    std::vector<PAttr> attrs;
    attrs.emplace_back("MW2R2_I32", DataType::dtInt32);
    attrs.emplace_back("MW2R2_I64", DataType::dtInt64);
    RecRule rule(attrs, align);

    const int    idI32      = PReg::getID("MW2R2_I32");
    const int    idI64      = PReg::getID("MW2R2_I64");
    const size_t recSize    = rule.getRecSize();
    const size_t kBuf       = 256000;
    const size_t kBatchSize = 4000;

    auto buffer = std::make_shared<RecBuffer>(rule, kBuf);

    RecordWriter writer1(buffer, kBatchSize);
    RecordWriter writer2(buffer, kBatchSize);
    ReaderT      reader1(buffer, kBatchSize);
    ReaderT      reader2(buffer, kBatchSize);

    using clock = std::chrono::steady_clock;
    const auto budget = std::chrono::milliseconds(3000);

    std::atomic<bool>     stopFlag{false};
    std::atomic<uint64_t> writtenA{0}, writtenB{0};
    std::atomic<uint64_t> readC{0},    readD{0};
    std::atomic<int64_t>  sinkC{0},    sinkD{0};

    auto writerTask = [&](RecordWriter& wr, std::atomic<uint64_t>& cnt) {
        uint64_t counter = 0;
        while (!stopFlag.load(std::memory_order_relaxed)) {
            auto batch = wr.nextBatch(kBatchSize, /*wait=*/true);
            if (!batch.isValid()) break;
            for (size_t i = 0; i < batch.capacity; ++i) {
                Record rec(batch.rule, batch.data + i * batch.recordSize);
                rec.setInt32(idI32, static_cast<int32_t>(counter));
                rec.setInt64(idI64, static_cast<int64_t>(counter));
                ++counter;
            }
            wr.commitBatch(batch.capacity);
        }
        wr.flush();
        cnt.store(counter);
    };

    auto readerTask = [&](ReaderT& rd, std::atomic<uint64_t>& cnt, std::atomic<int64_t>& sink) {
        int64_t  localSink = 0;
        uint64_t localCnt  = 0;
        while (!stopFlag.load(std::memory_order_relaxed)) {
            auto batch = rd.nextBatch(kBatchSize, /*wait=*/false);
            if (!batch.isValid()) { std::this_thread::yield(); continue; }
            for (size_t i = 0; i < batch.count; ++i) {
                Record rec(batch.rule,
                           const_cast<uint8_t*>(batch.data + i * batch.recordSize));
                localSink += rec.getInt32(idI32);
                localSink += rec.getInt64(idI64);
            }
            localCnt += batch.count;
        }
        cnt.store(localCnt);
        sink.store(localSink);
    };

    const auto runStart = clock::now();

    std::thread tw1([&] { writerTask(writer1, writtenA); });
    std::thread tw2([&] { writerTask(writer2, writtenB); });
    std::thread tr1([&] { readerTask(reader1, readC, sinkC); });
    std::thread tr2([&] { readerTask(reader2, readD, sinkD); });

    std::this_thread::sleep_for(budget);
    stopFlag.store(true, std::memory_order_relaxed);

    // Readers poll with wait=false so they exit quickly once stopFlag is set.
    tr1.join();
    tr2.join();
    // Unpin any ZC batch so that writer flush() can push remaining data.
    // For RecordReader this is a no-op.
    reader1.release();
    reader2.release();

    // Writer threads exit their loops, call flush(), then terminate.
    // Flush can now proceed because readers unpinned their cursors above.
    tw1.join();
    tw2.join();

    const auto elapsed   = clock::now() - runStart;
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    const uint64_t ms    = static_cast<uint64_t>(elapsedMs > 0 ? elapsedMs : 1);

    const uint64_t wA = writtenA.load();
    const uint64_t wB = writtenB.load();
    const uint64_t rC = readC.load();
    const uint64_t rD = readD.load();

    std::cout << "[DualW2R/" << label << "] budget: " << budget.count()
              << " ms, elapsed: " << elapsedMs << " ms"
              << ", recSize: " << recSize << " B"
              << ", batch: " << kBatchSize << ", buf: " << kBuf << "\n";
    std::cout << "[DualW2R/" << label << "] WriterA: " << wA
              << " rec (" << wA * 1000ULL / ms << " rec/s)\n";
    std::cout << "[DualW2R/" << label << "] WriterB: " << wB
              << " rec (" << wB * 1000ULL / ms << " rec/s)\n";
    std::cout << "[DualW2R/" << label << "] Total written: " << (wA + wB)
              << " rec (" << (wA + wB) * 1000ULL / ms << " rec/s combined)\n";
    std::cout << "[DualW2R/" << label << "] Reader1: " << rC
              << " rec (" << rC * 1000ULL / ms << " rec/s)"
              << ", read/written = "
              << (wA + wB > 0 ? static_cast<double>(rC) / static_cast<double>(wA + wB) : 0.0) << "\n";
    std::cout << "[DualW2R/" << label << "] Reader2: " << rD
              << " rec (" << rD * 1000ULL / ms << " rec/s)"
              << ", read/written = "
              << (wA + wB > 0 ? static_cast<double>(rD) / static_cast<double>(wA + wB) : 0.0) << "\n";
    std::cout << "[DualW2R/" << label << "] sink (ignore): "
              << sinkC.load() << ", " << sinkD.load() << "\n";

    EXPECT_GT(wA, 0u);
    EXPECT_GT(wB, 0u);
    EXPECT_GT(rC, 0u);
    EXPECT_GT(rD, 0u);
}

// =============================================================================
// Dual-writer / dual-reader test suite
// =============================================================================

class ZCDualWriterTest : public ::testing::TestWithParam<bool> {};

INSTANTIATE_TEST_SUITE_P(
    AlignModes,
    ZCDualWriterTest,
    ::testing::Values(false, true),
    [](const ::testing::TestParamInfo<bool>& info) {
        return info.param ? "Aligned" : "Packed";
    });

// Two buffered writers + two buffered readers sharing one buffer.
TEST_P(ZCDualWriterTest, DualWriter_Reader_Concurrent) {
    const bool align = GetParam();
    runDualWriterDualReader<RecordReader>(
        align, align ? "Writer+Reader Aligned" : "Writer+Reader Packed");
}

// Two buffered writers + two zero-copy readers sharing one buffer.
TEST_P(ZCDualWriterTest, DualWriter_ReaderZC_Concurrent) {
    const bool align = GetParam();
    runDualWriterDualReader<RecordReaderZC>(
        align, align ? "Writer+ReaderZC Aligned" : "Writer+ReaderZC Packed");
}
