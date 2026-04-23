// test_Core.cpp
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <algorithm>
#include <chrono>
#include <iostream>

#include "Core/PReg.h"
#include "Core/PAttr.h"
#include "Core/RecRule.h"
#include "Core/Record.h"
#include "Core/CircularBuffer.h"
#include "Core/RecBuffer.h"
#include "RecordWriter.h"
#include "RecordReader.h"

using namespace cyc;

// Tests that the parameter registry returns unique IDs for different names and consistent IDs for the same name.
TEST(PRegTest, UniqueIDs) {
    int id1 = PReg::getID("ParamA");
    int id2 = PReg::getID("ParamB");
    int id1_again = PReg::getID("ParamA");

    EXPECT_NE(id1, id2);
    EXPECT_EQ(id1, id1_again);
    EXPECT_EQ(PReg::getName(id1), "ParamA");
}

// Verifies the calculation of byte offsets for attributes within a record, including the implicit header.
TEST(RecRuleTest, OffsetCalculation) {
    std::vector<PAttr> attrs;
    attrs.emplace_back("IntVal", DataType::dtInt32, 1);
    attrs.emplace_back("DblVal", DataType::dtDouble, 1);
    attrs.emplace_back("StrVal", DataType::dtChar, 10);

    RecRule rule(attrs);
    const size_t HEADER_SIZE = 8; // TimeStamp is 8 bytes

    EXPECT_EQ(rule.getRecSize(), HEADER_SIZE + 4 + 8 + 10);

    EXPECT_EQ(rule.getOffsetById(PReg::getID("IntVal")), HEADER_SIZE + 0);
    EXPECT_EQ(rule.getOffsetById(PReg::getID("DblVal")), HEADER_SIZE + 4);
    EXPECT_EQ(rule.getOffsetById(PReg::getID("StrVal")), HEADER_SIZE + 12);
}

// Tests setting and retrieving values from a Record object using direct access and raw pointers.
TEST(RecordTest, DataAccess) {
    int idInt = PReg::getID("TestInt");
    int idDbl = PReg::getID("TestDbl");

    std::vector<PAttr> attrs;
    attrs.emplace_back("TestInt", DataType::dtInt32);
    attrs.emplace_back("TestDbl", DataType::dtDouble);
    RecRule rule(attrs);

    std::vector<uint8_t> rawData(rule.getRecSize());
    std::fill(rawData.begin(), rawData.end(), 0);

    Record rec(rule, rawData.data());

    rec.setInt32(idInt, 42);
    rec.setDouble(idDbl, 3.14159);

    EXPECT_EQ(rec.getInt32(idInt), 42);
    EXPECT_DOUBLE_EQ(rec.getDouble(idDbl), 3.14159);

    size_t offsetInt = rule.getOffsetById(idInt);
    ASSERT_GT(offsetInt, 0u);

    int32_t* rawIntPtr = reinterpret_cast<int32_t*>(rawData.data() + offsetInt);
    EXPECT_EQ(*rawIntPtr, 42);
}

// Verifies the basic FIFO behavior of the CircularBuffer.
TEST(CircularBufferTest, PushPopLogic) {
    CircularBuffer<int> buf(3);

    EXPECT_TRUE(buf.empty());

    buf.push_back(1);
    buf.push_back(2);
    buf.push_back(3);

    EXPECT_TRUE(buf.full());
    EXPECT_EQ(buf.size(), 3);

    EXPECT_EQ(buf.front(), 1);
    buf.pop_front();
    EXPECT_EQ(buf.front(), 2);
}

// Tests that the buffer correctly overwrites the oldest data when pushing to a full buffer.
TEST(CircularBufferTest, OverwriteBehavior) {
    CircularBuffer<int> buf(3);
    buf.push_back(1);
    buf.push_back(2);
    buf.push_back(3);
    buf.push_back(4);

    EXPECT_EQ(buf.size(), 3);
    EXPECT_EQ(buf.front(), 2);
    EXPECT_EQ(buf.back(), 4);
}

// Tests writing a batch of records to RecBuffer and reading a specific one back.
TEST(RecBufferTest, WriteAndReadRelative) {
    std::vector<PAttr> attrs = { PAttr("Val", DataType::dtInt32) };
    RecRule rule(attrs);

    RecBuffer buffer(rule, 10);

    size_t recSize = rule.getRecSize();
    std::vector<uint8_t> rawBatch(recSize * 5, 0);

    for(int i = 0; i < 5; ++i) {
        Record r(rule, rawBatch.data() + (i * recSize));
        r.setInt32(PReg::getID("Val"), (i + 1) * 10);
    }

    buffer.push(rawBatch.data(), 5);

    EXPECT_EQ(buffer.size(), 5);

    std::vector<uint8_t> readBuf(recSize);
    buffer.readRelative(1, readBuf.data(), 1);

    Record rRead(rule, readBuf.data());
    EXPECT_EQ(rRead.getInt32(PReg::getID("Val")), 20);
}

// =============================================================================
// BitField tests
// =============================================================================

// ---------------------------------------------------------------------------
// PAttr construction
// ---------------------------------------------------------------------------

// Verifies that constructing a bit-field PAttr with a non-integer type throws std::invalid_argument.
TEST(BitFieldTest, InvalidBaseTypeThrows) {
    EXPECT_THROW(
        PAttr("bad", DataType::dtFloat,  std::vector<std::string>{"flag1"}),
        std::invalid_argument);

    EXPECT_THROW(
        PAttr("bad", DataType::dtDouble, std::vector<std::string>{"flag1"}),
        std::invalid_argument);

    EXPECT_THROW(
        PAttr("bad", DataType::dtPtr,    std::vector<std::string>{"flag1"}),
        std::invalid_argument);
}

// Verifies that all integer types are accepted as a base for bit fields without throwing.
TEST(BitFieldTest, ValidBaseTypesDoNotThrow) {
    EXPECT_NO_THROW(PAttr("r8s",  DataType::dtInt8,   std::vector<std::string>{"b0"}));
    EXPECT_NO_THROW(PAttr("r8u",  DataType::dtUInt8,  std::vector<std::string>{"b0"}));
    EXPECT_NO_THROW(PAttr("r16s", DataType::dtInt16,  std::vector<std::string>{"b0"}));
    EXPECT_NO_THROW(PAttr("r16u", DataType::dtUInt16, std::vector<std::string>{"b0"}));
    EXPECT_NO_THROW(PAttr("r32s", DataType::dtInt32,  std::vector<std::string>{"b0"}));
    EXPECT_NO_THROW(PAttr("r32u", DataType::dtUInt32, std::vector<std::string>{"b0"}));
    EXPECT_NO_THROW(PAttr("r64s", DataType::dtInt64,  std::vector<std::string>{"b0"}));
    EXPECT_NO_THROW(PAttr("r64u", DataType::dtUInt64, std::vector<std::string>{"b0"}));
}

// Verifies that a bitDefs list that would overflow the base type width throws std::out_of_range.
TEST(BitFieldTest, OverflowBitDefsThrows) {
    // UInt8 = 8 bits; 9 named bits must throw
    EXPECT_THROW(
        PAttr("ovf", DataType::dtUInt8,
              std::vector<std::string>{"b0","b1","b2","b3","b4","b5","b6","b7","b8"}),
        std::out_of_range);

    // Numeric skip that pushes past the boundary must also throw
    EXPECT_THROW(
        PAttr("ovf2", DataType::dtUInt8,
              std::vector<std::string>{"flag", "8"}),   // flag at bit 0, then skip 8 -> bit 9 > 8
        std::out_of_range);
}

// Verifies that a numeric skip of "0" is accepted as a no-op.
TEST(BitFieldTest, ZeroSkipIsNoOp) {
    EXPECT_NO_THROW(
        PAttr("noop", DataType::dtUInt8,
              std::vector<std::string>{"f0","0","f1"}));  // "0" between two named bits
}

// ---------------------------------------------------------------------------
// RecRule caching
// ---------------------------------------------------------------------------

// Verifies that getBitRef returns the correct containing fieldId and bit position.
TEST(BitFieldTest, RecRuleBitRefCache) {
    //   UInt8: [tx | rx | 4-bit-skip | err | (implicit)]
    //           bit0  bit1  bits2-5      bit6   bit7
    PAttr sr("SR1", DataType::dtUInt8,
             std::vector<std::string>{"bf_tx", "bf_rx", "4", "bf_err"});

    RecRule rule({sr});

    int idField = PReg::getID("SR1");
    int idTx    = PReg::getID("bf_tx");
    int idRx    = PReg::getID("bf_rx");
    int idErr   = PReg::getID("bf_err");

    // All three named bits must resolve to the same containing field
    BitRef refTx  = rule.getBitRef(idTx);
    BitRef refRx  = rule.getBitRef(idRx);
    BitRef refErr = rule.getBitRef(idErr);

    EXPECT_EQ(refTx.fieldId,  idField);
    EXPECT_EQ(refRx.fieldId,  idField);
    EXPECT_EQ(refErr.fieldId, idField);

    EXPECT_EQ(refTx.bitPos,  0);
    EXPECT_EQ(refRx.bitPos,  1);
    EXPECT_EQ(refErr.bitPos, 6);  // 2 named + 4 skipped = bit 6

    // An ordinary (non-bit) ID must return an invalid BitRef
    BitRef refField = rule.getBitRef(idField);
    EXPECT_EQ(refField.fieldId, 0);
}

// ---------------------------------------------------------------------------
// Record read / write
// ---------------------------------------------------------------------------

// Tests that getBit returns false for all bits of a zero-initialised record.
TEST(BitFieldTest, InitiallyAllBitsZero) {
    PAttr sr("SR2", DataType::dtUInt8,
             std::vector<std::string>{"c0","c1","c2","c3","c4","c5","c6","c7"});
    RecRule rule({sr});

    std::vector<uint8_t> raw(rule.getRecSize(), 0);
    Record rec(rule, raw.data());

    for (const char* name : {"c0","c1","c2","c3","c4","c5","c6","c7"}) {
        EXPECT_FALSE(rec.getBit(PReg::getID(name))) << "Bit " << name << " should be 0";
    }
}

// Tests that setting individual bits does not affect neighbouring bits.
TEST(BitFieldTest, BitIsolation) {
    PAttr sr("SR3", DataType::dtUInt8,
             std::vector<std::string>{"d0","d1","d2","d3","d4","d5","d6","d7"});
    RecRule rule({sr});

    std::vector<uint8_t> raw(rule.getRecSize(), 0);
    Record rec(rule, raw.data());

    int ids[8];
    const char* names[8] = {"d0","d1","d2","d3","d4","d5","d6","d7"};
    for (int i = 0; i < 8; ++i) ids[i] = PReg::getID(names[i]);

    // Set each bit in turn and verify only that bit is set
    for (int setIdx = 0; setIdx < 8; ++setIdx) {
        std::fill(raw.begin(), raw.end(), 0);

        rec.setBit(ids[setIdx], true);

        for (int checkIdx = 0; checkIdx < 8; ++checkIdx) {
            if (checkIdx == setIdx)
                EXPECT_TRUE(rec.getBit(ids[checkIdx]))
                    << "Bit " << names[checkIdx] << " should be set";
            else
                EXPECT_FALSE(rec.getBit(ids[checkIdx]))
                    << "Bit " << names[checkIdx] << " should be clear";
        }
    }
}

// Tests that clearing a bit works correctly while leaving other bits intact.
TEST(BitFieldTest, SetAndClearBit) {
    PAttr sr("SR4", DataType::dtUInt8,
             std::vector<std::string>{"e0","e1","e2"});
    RecRule rule({sr});

    std::vector<uint8_t> raw(rule.getRecSize(), 0);
    Record rec(rule, raw.data());

    int idE0 = PReg::getID("e0");
    int idE1 = PReg::getID("e1");
    int idE2 = PReg::getID("e2");

    rec.setBit(idE0, true);
    rec.setBit(idE1, true);
    rec.setBit(idE2, true);

    rec.setBit(idE1, false);

    EXPECT_TRUE (rec.getBit(idE0));
    EXPECT_FALSE(rec.getBit(idE1));
    EXPECT_TRUE (rec.getBit(idE2));
}

// Tests the double-based convenience wrappers getBitValue / setBitValue.
TEST(BitFieldTest, DoubleWrappers) {
    PAttr sr("SR5", DataType::dtUInt8, std::vector<std::string>{"f0","f1"});
    RecRule rule({sr});

    std::vector<uint8_t> raw(rule.getRecSize(), 0);
    Record rec(rule, raw.data());

    int idF0 = PReg::getID("f0");
    int idF1 = PReg::getID("f1");

    rec.setBitValue(idF0, 1.0);
    rec.setBitValue(idF1, 0.0);

    EXPECT_DOUBLE_EQ(rec.getBitValue(idF0), 1.0);
    EXPECT_DOUBLE_EQ(rec.getBitValue(idF1), 0.0);

    // Any non-zero value must set the bit
    rec.setBitValue(idF1, -3.5);
    EXPECT_TRUE(rec.getBit(idF1));
}

// Verifies that bit operations on a 16-bit field correctly span both bytes.
TEST(BitFieldTest, WideBaseType_UInt16) {
    // UInt16 = 16 bits; name bits 0 and 15 (the LSB and MSB of the field)
    PAttr sr("SR6", DataType::dtUInt16,
             std::vector<std::string>{"g0", "14", "g15"});  // bit0, skip 14, bit15
    RecRule rule({sr});

    std::vector<uint8_t> raw(rule.getRecSize(), 0);
    Record rec(rule, raw.data());

    int idG0  = PReg::getID("g0");
    int idG15 = PReg::getID("g15");

    rec.setBit(idG0,  true);
    rec.setBit(idG15, true);

    EXPECT_TRUE(rec.getBit(idG0));
    EXPECT_TRUE(rec.getBit(idG15));

    // Verify raw value: bit0=1 + bit15=1 → 0x8001
    size_t off = rule.getOffsetById(PReg::getID("SR6"));
    uint16_t raw16;
    std::memcpy(&raw16, raw.data() + off, sizeof(raw16));
    EXPECT_EQ(raw16, static_cast<uint16_t>(0x8001));
}

// Verifies that bit fields and plain fields coexist correctly within the same record.
TEST(BitFieldTest, BitFieldsAlongsidePlainFields) {
    PAttr sr ("SR7",    DataType::dtUInt8,  std::vector<std::string>{"h0","h1"});
    PAttr val("PlainV", DataType::dtInt32,  1);
    RecRule rule({sr, val});

    std::vector<uint8_t> raw(rule.getRecSize(), 0);
    Record rec(rule, raw.data());

    int idH0    = PReg::getID("h0");
    int idH1    = PReg::getID("h1");
    int idPlain = PReg::getID("PlainV");

    rec.setBit(idH0, true);
    rec.setInt32(idPlain, 0xDEADBEEF);

    // Setting a plain field must not corrupt the bit field and vice-versa
    EXPECT_TRUE (rec.getBit(idH0));
    EXPECT_FALSE(rec.getBit(idH1));
    EXPECT_EQ   (rec.getInt32(idPlain), static_cast<int32_t>(0xDEADBEEF));
}

// Verifies that constructing a RecRule with the same bit name in two different
// fields throws std::invalid_argument.
TEST(BitFieldTest, DuplicateBitAcrossFieldsThrows) {
    // "dup" appears in both SR_A and SR_B — must throw on RecRule construction
    PAttr srA("SR_A", DataType::dtUInt8,  std::vector<std::string>{"dup", "other_a"});
    PAttr srB("SR_B", DataType::dtUInt8,  std::vector<std::string>{"other_b", "dup"});

    EXPECT_THROW(RecRule({srA, srB}), std::invalid_argument);
}

// Verifies that constructing a RecRule with unique bit names does not throw.
TEST(BitFieldTest, UniqueBitsAcrossFieldsDoNotThrow) {
    PAttr srA("SR_X", DataType::dtUInt8, std::vector<std::string>{"xa","xb"});
    PAttr srB("SR_Y", DataType::dtUInt8, std::vector<std::string>{"ya","yb"});

    EXPECT_NO_THROW(RecRule({srA, srB}));
}

// Verifies that looking up an ID that belongs to a plain field returns an invalid BitRef.
TEST(BitFieldTest, LookupOfPlainFieldIdReturnsBadRef) {
    PAttr plain("PlainQ", DataType::dtInt32, 1);
    RecRule rule({plain});

    int idPlain = PReg::getID("PlainQ");
    BitRef ref  = rule.getBitRef(idPlain);

    EXPECT_EQ(ref.fieldId, 0);
}

// Verifies that the schema survives a toText/fromText round-trip with bit field info intact.
// Also checks that the serialised text uses human-readable skip notation, not empty columns.
TEST(BitFieldTest, SerialiseRoundTrip) {
    // Layout:  bit0=i0, bit1=i1, bits2-3 skipped ("2"), bit4=i4, bits5-7 skipped ("3")
    PAttr sr ("SR8",   DataType::dtUInt8,  std::vector<std::string>{"i0","i1","2","i4"});
    PAttr val("IntQ",  DataType::dtInt32,  1);
    RecRule original({sr, val});

    std::string text = original.toText();

    // --- Verify human-readable format ---
    // The bit-field column must contain "i0,i1,2,i4,3" (skip runs as numbers,
    // NOT sparse comma-separated with empty slots like "i0,i1,,,i4,,,")
    EXPECT_NE(text.find("i0,i1,2,i4,3"), std::string::npos)
        << "toText() should encode skipped bits as numbers, got:\n" << text;

    // Must not contain adjacent commas (empty columns)
    EXPECT_EQ(text.find(",,"), std::string::npos)
        << "toText() must not produce empty columns, got:\n" << text;

    // --- Verify round-trip correctness ---
    RecRule restored = RecRule::fromText(text);

    EXPECT_EQ(restored.getRecSize(), original.getRecSize());

    int idI0 = PReg::getID("i0");
    int idI1 = PReg::getID("i1");
    int idI4 = PReg::getID("i4");

    BitRef r0 = restored.getBitRef(idI0);
    BitRef r1 = restored.getBitRef(idI1);
    BitRef r4 = restored.getBitRef(idI4);

    EXPECT_GT(r0.fieldId, 0);
    EXPECT_EQ(r0.bitPos, 0);
    EXPECT_EQ(r1.bitPos, 1);
    EXPECT_EQ(r4.bitPos, 4);  // 2 named + "2" skip = bit 4

    // Plain field must still be accessible at the same offset
    EXPECT_EQ(restored.getOffsetById(PReg::getID("IntQ")),
              original.getOffsetById(PReg::getID("IntQ")));

    // Bit operations on the restored schema must work correctly
    std::vector<uint8_t> raw(restored.getRecSize(), 0);
    Record rec(restored, raw.data());
    rec.setBit(idI0, true);
    rec.setBit(idI4, true);
    EXPECT_TRUE (rec.getBit(idI0));
    EXPECT_FALSE(rec.getBit(idI1));
    EXPECT_TRUE (rec.getBit(idI4));
}

// =============================================================================
// Async / parameterised tests
// =============================================================================

class AsyncIntegrationTest : public ::testing::TestWithParam<std::tuple<double, double>> {};

INSTANTIATE_TEST_SUITE_P(
    StandardSuite,
    AsyncIntegrationTest,
    ::testing::Combine(
        ::testing::Values(0.2, 1),
        ::testing::Values(0.2, 1)
        )
    );

// Validates the interaction between separate producer and consumer threads using RecordWriter/Reader.
TEST_P(AsyncIntegrationTest, WriteReadFlow) {
    int idVal = PReg::getID("Val");
    std::vector<PAttr> attrs = { PAttr("Val", DataType::dtInt32) };
    RecRule rule(attrs);

    double p1 = std::get<0>(GetParam());
    double p2 = std::get<1>(GetParam());

    std::shared_ptr<RecBuffer> mainBuffer = std::make_shared<RecBuffer>(rule, 50000);

    RecordWriter writer(mainBuffer, static_cast<size_t>(10000 * p1));
    RecordReader reader(mainBuffer, static_cast<size_t>(10000 * p2));

    const int TOTAL_RECORDS = 100000;

    std::thread producer([&]() {
        for(int i = 0; i < TOTAL_RECORDS; ++i) {
            Record r = writer.nextRecord();
            r.setInt32(idVal, i);
            writer.commitRecord();
        }
        writer.flush();
    });

    std::vector<int> receivedData(TOTAL_RECORDS, 0);
    for (int i = 0; i < TOTAL_RECORDS; ++i) {
        Record r = reader.nextRecord();
        receivedData[i] = r.getInt32(idVal);
    }

    producer.join();

    ASSERT_EQ(receivedData.size(), TOTAL_RECORDS);
    for(int i = 0; i < TOTAL_RECORDS; ++i) {
        ASSERT_EQ(receivedData[i], i);
    }
}

// =============================================================================
// Record field read/write throughput benchmark
// =============================================================================

// Parameterised over a single bool: the RecRule alignment flag. Runs once with
// the historical packed layout and once with the aligned layout so the two
// numbers can be compared directly.
class RecordPerformanceTest : public ::testing::TestWithParam<bool> {};

INSTANTIATE_TEST_SUITE_P(
    AlignModes,
    RecordPerformanceTest,
    ::testing::Values(false, true),
    [](const ::testing::TestParamInfo<bool>& info) {
        return info.param ? "Aligned" : "Packed";
    });

// Alternates 1000-record write chunks with 1000-record read chunks within a
// fixed time budget, accumulating write and read times separately. One "cycle"
// writes (or reads) every field of the record once. Results are printed to
// stdout so the tester can gauge throughput.
TEST_P(RecordPerformanceTest, FieldReadWriteThroughput) {
    const bool align = GetParam();

    std::vector<PAttr> attrs;
    attrs.emplace_back("PerfI8",  DataType::dtInt8);
    attrs.emplace_back("PerfU8",  DataType::dtUInt8);
    attrs.emplace_back("PerfI16", DataType::dtInt16);
    attrs.emplace_back("PerfU32", DataType::dtInt32, 2);
    attrs.emplace_back("PerfI32", DataType::dtUInt32);
    attrs.emplace_back("PerfI64", DataType::dtInt64);
    attrs.emplace_back("PerfFlt", DataType::dtFloat, 2);
    attrs.emplace_back("PerfDbl", DataType::dtDouble, 2);
    RecRule rule(attrs, align);

    const int idI8  = PReg::getID("PerfI8");
    const int idU8  = PReg::getID("PerfU8");
    const int idI16 = PReg::getID("PerfI16");
    const int idI32 = PReg::getID("PerfI32");
    const int idU32 = PReg::getID("PerfUI32");
    const int idI64 = PReg::getID("PerfI64");
    const int idFlt = PReg::getID("PerfFlt");
    const int idDbl = PReg::getID("PerfDbl");

    // Back the benchmark with a buffer of many records so each cycle touches a
    // different memory slot. This forces real memory traffic and is what makes
    // the packed-vs-aligned comparison meaningful — iterating a single record
    // would just keep one cache line hot and hide layout effects.
    const size_t recSize       = rule.getRecSize();
    const size_t kRecordCount  = 128000;   // ~280-320 KB — overflows L1, fits L2
    const size_t kChunkSize    = 4000;
    std::shared_ptr<RecBuffer> buffer = std::make_shared<RecBuffer>(rule, kRecordCount);

    using clock = std::chrono::steady_clock;
    const auto budget = std::chrono::milliseconds(1000);
    const size_t fieldsPerCycle = attrs.size();

    RecordWriter recWriter(buffer, kChunkSize);
    RecordReader recReader(buffer, kChunkSize);

    volatile int64_t sink = 0;                 // defeats dead-store elimination
    uint64_t totalRecords = 0;
    clock::duration writeElapsed{0};
    clock::duration readElapsed{0};
    uint64_t counter = 0;

    const auto loopStart = clock::now();
    const auto loopDeadline = loopStart + budget;
    while (clock::now() < loopDeadline) {
        // --- Write 1000 records ---
        const auto writeBegin = clock::now();
        for (size_t i = 0; i < kChunkSize; ++i) {
            auto rec = recWriter.nextRecord();
            rec.setInt8 (idI8,  static_cast<int8_t >(counter));
            rec.setInt8 (idU8,  static_cast<uint8_t >(counter));
            rec.setInt16(idI16, static_cast<int16_t>(counter));
            rec.setInt32(idI32, static_cast<int32_t>(counter));
            rec.setInt32(idU32, static_cast<uint32_t>(counter), 1);
            rec.setInt32(idU32, static_cast<uint32_t>(counter), 3);
            rec.setInt64(idI64, static_cast<int64_t>(counter));
            rec.setFloat(idFlt, static_cast<float  >(counter), 1);
            rec.setFloat(idFlt, static_cast<float  >(counter), 2);
            rec.setDouble(idDbl, static_cast<double>(counter), 1);
            rec.setDouble(idDbl, static_cast<double>(counter), 2);
            ++counter;
            recWriter.commitRecord();
        }
        recWriter.flush();  // make the chunk visible to the reader
        writeElapsed += clock::now() - writeBegin;

        // --- Read 1000 records ---
        const auto readBegin = clock::now();
        for (size_t i = 0; i < kChunkSize; ++i) {
            auto rec = recReader.nextRecord();
            sink += rec.getInt8 (idI8);
            sink += rec.getUInt8 (idU8);
            sink += rec.getInt16(idI16);
            sink += rec.getInt32(idI32);
            sink += rec.getUInt32(idU32, 1);
            sink += rec.getUInt32(idU32, 2);
            sink += rec.getInt64(idI64);
            sink += static_cast<int64_t>(rec.getFloat (idFlt),1);
            sink += static_cast<int64_t>(rec.getFloat (idFlt),2);
            sink += static_cast<int64_t>(rec.getDouble(idDbl),1);
            sink += static_cast<int64_t>(rec.getDouble(idDbl),2);
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

    const char* mode = align ? "Aligned" : "Packed";
    std::cout << "[Perf/" << mode << "] budget: " << budget.count()
              << " ms, fields/cycle: " << fieldsPerCycle
              << ", recSize: " << recSize << " bytes"
              << ", chunk: " << kChunkSize
              << ", bufferRecords: " << kRecordCount
              << ", workingSet: " << (recSize * kRecordCount) / 1024 << " KiB\n";
    std::cout << "[Perf/" << mode << "] Total records processed: " << totalRecords
              << " (" << totalBytes / 1048576 << " MiB per side)\n";
    std::cout << "[Perf/" << mode << "] Write: " << writeMs << " ms"
              << "  (" << writesPerSec << " records/s, "
              <<  writesPerSec * fieldsPerCycle << " field-writes/s, "
              << writeMiBps << " MiB/s)\n";
    std::cout << "[Perf/" << mode << "] Read : " << readMs  << " ms"
              << "  (" << readsPerSec  << " records/s, "
              <<  readsPerSec  * fieldsPerCycle << " field-reads/s, "
              << readMiBps << " MiB/s)\n";
    std::cout << "[Perf/" << mode << "] sink (ignore): " << sink << '\n';

    EXPECT_GT(totalRecords, 0u);
}

// Same per-record work as FieldReadWriteThroughput, but the write and read
// loops run on separate threads for the full budget window. Measures how much
// the reader manages to consume while the writer is producing concurrently.
TEST_P(RecordPerformanceTest, ConcurrentFieldReadWriteThroughput) {
    const bool align = GetParam();

    std::vector<PAttr> attrs;
    attrs.emplace_back("PerfI8",  DataType::dtInt8);
    attrs.emplace_back("PerfU8",  DataType::dtUInt8);
    attrs.emplace_back("PerfI16", DataType::dtInt16);
    attrs.emplace_back("PerfU32", DataType::dtInt32, 2);
    attrs.emplace_back("PerfI32", DataType::dtUInt32);
    attrs.emplace_back("PerfI64", DataType::dtInt64);
    attrs.emplace_back("PerfFlt", DataType::dtFloat, 2);
    attrs.emplace_back("PerfDbl", DataType::dtDouble, 2);
    RecRule rule(attrs, align);

    const int idI8  = PReg::getID("PerfI8");
    const int idU8  = PReg::getID("PerfU8");
    const int idI16 = PReg::getID("PerfI16");
    const int idI32 = PReg::getID("PerfI32");
    const int idU32 = PReg::getID("PerfUI32");
    const int idI64 = PReg::getID("PerfI64");
    const int idFlt = PReg::getID("PerfFlt");
    const int idDbl = PReg::getID("PerfDbl");

    const size_t recSize       = rule.getRecSize();
    const size_t kRecordCount  = 128000;
    const size_t kChunkSize    = 4000;
    std::shared_ptr<RecBuffer> buffer = std::make_shared<RecBuffer>(rule, kRecordCount);

    using clock = std::chrono::steady_clock;
    const auto budget = std::chrono::milliseconds(1000);
    const size_t fieldsPerCycle = attrs.size();

    RecordWriter recWriter(buffer, kChunkSize);
    RecordReader recReader(buffer, kChunkSize);

    std::atomic<bool> stopFlag{false};
    std::atomic<uint64_t> writtenRecords{0};
    std::atomic<uint64_t> readRecords{0};
    std::atomic<int64_t>  sinkTotal{0};

    std::thread writerThread([&]() {
        uint64_t counter = 0;
        while (!stopFlag.load(std::memory_order_relaxed)) {
            for (size_t i = 0; i < kChunkSize; ++i) {
                auto rec = recWriter.nextRecord();
                rec.setInt8 (idI8,  static_cast<int8_t >(counter));
                rec.setInt8 (idU8,  static_cast<uint8_t >(counter));
                rec.setInt16(idI16, static_cast<int16_t>(counter));
                rec.setInt32(idI32, static_cast<int32_t>(counter));
                rec.setInt32(idU32, static_cast<uint32_t>(counter), 1);
                rec.setInt32(idU32, static_cast<uint32_t>(counter), 3);
                rec.setInt64(idI64, static_cast<int64_t>(counter));
                rec.setFloat(idFlt, static_cast<float  >(counter), 1);
                rec.setFloat(idFlt, static_cast<float  >(counter), 2);
                rec.setDouble(idDbl, static_cast<double>(counter), 1);
                rec.setDouble(idDbl, static_cast<double>(counter), 2);
                ++counter;
                recWriter.commitRecord();
            }
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
                sink += rec.getInt8  (idI8);
                sink += rec.getUInt8 (idU8);
                sink += rec.getInt16 (idI16);
                sink += rec.getInt32 (idI32);
                sink += rec.getUInt32(idU32, 1);
                sink += rec.getUInt32(idU32, 2);
                sink += rec.getInt64 (idI64);
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

    const uint64_t written = writtenRecords.load();
    const uint64_t readCnt = readRecords.load();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    const uint64_t writesPerSec = elapsedMs > 0 ? (written * 1000ULL) / static_cast<uint64_t>(elapsedMs) : 0;
    const uint64_t readsPerSec  = elapsedMs > 0 ? (readCnt * 1000ULL) / static_cast<uint64_t>(elapsedMs) : 0;
    const double   readRatio    = written > 0 ? (static_cast<double>(readCnt) / static_cast<double>(written)) : 0.0;
    const uint64_t writtenBytes = written * static_cast<uint64_t>(recSize);
    const uint64_t readBytes    = readCnt * static_cast<uint64_t>(recSize);
    const double   writeMiBps   = elapsedMs > 0 ? (static_cast<double>(writtenBytes) / 1048576.0) * 1000.0 / static_cast<double>(elapsedMs) : 0.0;
    const double   readMiBps    = elapsedMs > 0 ? (static_cast<double>(readBytes)    / 1048576.0) * 1000.0 / static_cast<double>(elapsedMs) : 0.0;

    const char* mode = align ? "Aligned" : "Packed";
    std::cout << "[PerfConc/" << mode << "] budget: " << budget.count()
              << " ms, elapsed: " << elapsedMs
              << " ms, fields/cycle: " << fieldsPerCycle
              << ", recSize: " << recSize << " bytes"
              << ", chunk: " << kChunkSize
              << ", bufferRecords: " << kRecordCount
              << ", workingSet: " << (recSize * kRecordCount) / 1024 << " KiB\n";
    std::cout << "[PerfConc/" << mode << "] Written: " << written
              << " (" << writtenBytes / 1048576 << " MiB)"
              << "  (" << writesPerSec << " records/s, "
              << writesPerSec * fieldsPerCycle << " field-writes/s, "
              << writeMiBps << " MiB/s)\n";
    std::cout << "[PerfConc/" << mode << "] Read   : " << readCnt
              << " (" << readBytes / 1048576 << " MiB)"
              << "  (" << readsPerSec  << " records/s, "
              << readsPerSec  * fieldsPerCycle << " field-reads/s, "
              << readMiBps << " MiB/s)"
              << ", read/written = " << readRatio << "\n";
    std::cout << "[PerfConc/" << mode << "] sink (ignore): " << sinkTotal.load() << '\n';

    EXPECT_GT(written, 0u);
    EXPECT_GT(readCnt, 0u);
}

// =============================================================================
// RecRule aligned-layout tests
// =============================================================================
// These tests live at the bottom of the file so that the PAttr names they
// register do not shift the PReg IDs used by the performance benchmark above —
// keeping benchmark conditions stable across runs.

// Verifies aligned layout: user fields are sorted by decreasing element size
// (so each one lands on a naturally-aligned offset without internal padding),
// and the total record size is a multiple of the largest element size so that
// consecutive records in a packed buffer stay on the same boundary.
TEST(RecRuleTest, AlignedLayout) {
    std::vector<PAttr> attrs;
    attrs.emplace_back("AL_I8",  DataType::dtInt8,   1);   // 1 byte
    attrs.emplace_back("AL_I32", DataType::dtInt32,  1);   // 4 bytes
    attrs.emplace_back("AL_Dbl", DataType::dtDouble, 1);   // 8 bytes
    attrs.emplace_back("AL_I16", DataType::dtInt16,  1);   // 2 bytes

    RecRule rule(attrs, /*align=*/true);

    // Expected layout after sort-by-desc-elem-size (header stays first):
    //   TimeStamp (dtDouble) @ 0     (8)
    //   AL_Dbl               @ 8     (8)
    //   AL_I32               @ 16    (4)
    //   AL_I16               @ 20    (2)
    //   AL_I8                @ 22    (1)
    //   tail padding                 (1)  → total 24 = multiple of 8
    EXPECT_EQ(rule.getOffsetById(PReg::getID("TimeStamp")), 0u);
    EXPECT_EQ(rule.getOffsetById(PReg::getID("AL_Dbl")),    8u);
    EXPECT_EQ(rule.getOffsetById(PReg::getID("AL_I32")),   16u);
    EXPECT_EQ(rule.getOffsetById(PReg::getID("AL_I16")),   20u);
    EXPECT_EQ(rule.getOffsetById(PReg::getID("AL_I8")),    22u);

    EXPECT_EQ(rule.getRecSize() % 8u, 0u);
    EXPECT_EQ(rule.getRecSize(), 24u);
}

// Verifies that an array field does not break natural alignment of the next
// smaller field (total size of each field is always a multiple of its own
// element size, so sequential placement after a desc-size sort stays aligned).
TEST(RecRuleTest, AlignedArrayField) {
    std::vector<PAttr> attrs;
    attrs.emplace_back("AA_Str", DataType::dtChar,  3);   // 3 bytes, elem align 1
    attrs.emplace_back("AA_I32", DataType::dtInt32, 1);   // 4 bytes, elem align 4
    attrs.emplace_back("AA_I16", DataType::dtInt16, 1);   // 2 bytes, elem align 2

    RecRule rule(attrs, /*align=*/true);

    // Sorted by elem size desc: I32(4), I16(2), Str(elem=1).
    // Header Double(8), I32@8, I16@12, Str@14 → 17 bytes → tail pad to 24 (mult of 8).
    EXPECT_EQ(rule.getOffsetById(PReg::getID("AA_I32")),  8u);
    EXPECT_EQ(rule.getOffsetById(PReg::getID("AA_I16")), 12u);
    EXPECT_EQ(rule.getOffsetById(PReg::getID("AA_Str")), 14u);
    EXPECT_EQ(rule.getRecSize() % 8u, 0u);
    EXPECT_EQ(rule.getRecSize(), 24u);
}

// Verifies that the default (align=false) path keeps the historical packed
// layout and the original field order unchanged.
TEST(RecRuleTest, PackedLayoutUnchanged) {
    std::vector<PAttr> attrs;
    attrs.emplace_back("PK_I8",  DataType::dtInt8,   1);
    attrs.emplace_back("PK_I32", DataType::dtInt32,  1);
    attrs.emplace_back("PK_Dbl", DataType::dtDouble, 1);

    RecRule rule(attrs);  // default align=false

    const size_t header = 8;
    EXPECT_EQ(rule.getOffsetById(PReg::getID("PK_I8")),  header + 0u);
    EXPECT_EQ(rule.getOffsetById(PReg::getID("PK_I32")), header + 1u);
    EXPECT_EQ(rule.getOffsetById(PReg::getID("PK_Dbl")), header + 5u);
    EXPECT_EQ(rule.getRecSize(), header + 1u + 4u + 8u);
}
