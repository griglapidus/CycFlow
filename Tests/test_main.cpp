#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <chrono>
#include <algorithm>

#include "PReg.h"
#include "PAttr.h"
#include "RecRule.h"
#include "Record.h"
#include "CircularBuffer.h"
#include "RecBuffer.h"
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

// Verifies the calculation of byte offsets for attributes within a record, including the header.
TEST(RecRuleTest, OffsetCalculation) {
    std::vector<PAttr> attrs;
    attrs.emplace_back("IntVal", DataType::dtInt32, 1);
    attrs.emplace_back("DblVal", DataType::dtDouble, 1);
    attrs.emplace_back("StrVal", DataType::dtChar, 10);

    RecRule rule(attrs);
    const size_t HEADER_SIZE = 8;

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

    for(int i=0; i<5; ++i) {
        Record r(rule, rawBatch.data() + (i * recSize));
        r.setInt32(PReg::getID("Val"), (i+1)*10);
    }

    buffer.push(rawBatch.data(), 5);

    EXPECT_EQ(buffer.size(), 5);

    std::vector<uint8_t> readBuf(recSize);
    buffer.readRelative(1, readBuf.data(), 1);

    Record rRead(rule, readBuf.data());
    EXPECT_EQ(rRead.getInt32(PReg::getID("Val")), 20);
}

// Validates the interaction between separate producer and consumer threads using RecordWriter/Reader.
TEST(AsyncIntegrationTest, WriteReadFlow) {
    int idVal = PReg::getID("Val");
    std::vector<PAttr> attrs = { PAttr("Val", DataType::dtInt32) };
    RecRule rule(attrs);

    std::shared_ptr<RecBuffer> mainBuffer = std::make_shared<RecBuffer>(rule, 1000);

    RecordWriter writer(mainBuffer, 100);
    RecordReader reader(mainBuffer, 100);

    const int TOTAL_RECORDS = 100;

    std::thread producer([&]() {
        for(int i = 0; i < TOTAL_RECORDS; ++i) {
            Record r = writer.nextRecord();
            r.setInt32(idVal, i);
            writer.commitRecord();
        }
        writer.flush();
    });

    std::vector<int> receivedData;
    for (int i = 0; i < TOTAL_RECORDS; ++i) {
        Record r = reader.nextRecord();
        receivedData.push_back(r.getInt32(idVal));
        if (i % 10 == 0) reader.notifyDataAvailable();
    }

    producer.join();

    ASSERT_EQ(receivedData.size(), TOTAL_RECORDS);
    for(int i = 0; i < TOTAL_RECORDS; ++i) {
        ASSERT_EQ(receivedData[i], i);
    }
}
