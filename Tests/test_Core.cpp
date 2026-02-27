// test_Core.cpp
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <algorithm>

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
