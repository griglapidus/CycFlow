// test_Cbf.cpp
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <cstdio>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

#include "Cbf/CbfFile.h"
#include "Cbf/CbfReader.h"
#include "Cbf/CbfWriter.h"
#include "Core/RecBuffer.h"
#include "Core/RecRule.h"
#include "Core/Record.h"
#include "Core/PAttr.h"
#include "Core/PReg.h"
#include "RecordWriter.h"

using namespace cyc;

// =========================================================================
// LOW-LEVEL CBF FILE TESTS
// =========================================================================

class CbfFileTest : public ::testing::Test {
protected:
    std::string testFileName = "test_data_file.cbf";

    void TearDown() override {
        std::remove(testFileName.c_str());
    }
};

TEST_F(CbfFileTest, WriteAndReadCycle) {
    // 1. Prepare Schema (RecRule automatically adds TimeStamp as ID=1)
    std::vector<PAttr> attrs = {
        {"SensorValue", DataType::dtInt32, 1},
        {"Voltage", DataType::dtDouble, 1}
    };
    RecRule writeRule(attrs);
    auto tsId = writeRule.getAttributes()[0].id;
    auto sensorValueId = writeRule.getAttributes()[1].id;
    auto voltageId = writeRule.getAttributes()[2].id;

    // 2. Write Data
    {
        CbfFile writer;
        ASSERT_TRUE(writer.open(testFileName, CbfMode::Write));
        ASSERT_TRUE(writer.isOpen());

        std::string myAlias = "Engine_1";
        writer.setAlias(myAlias);
        ASSERT_TRUE(writer.writeHeader(writeRule));
        ASSERT_TRUE(writer.beginDataSection());

        std::vector<uint8_t> buffer(writeRule.getRecSize());
        Record rec(writeRule, buffer.data());

        // Record 1
        rec.clear();
        rec.setValue(tsId, 123456789.0);
        rec.setValue(sensorValueId, 42.0);
        rec.setValue(voltageId, 3.14);
        ASSERT_TRUE(writer.writeRecord(rec));

        // Record 2
        rec.clear();
        rec.setValue(tsId, 123456790.0);
        rec.setValue(sensorValueId, 100.0);
        rec.setValue(voltageId, 5.55);
        ASSERT_TRUE(writer.writeRecord(rec));

        ASSERT_TRUE(writer.endDataSection());
        writer.close();
    }

    // 3. Read Data
    {
        CbfFile reader;
        ASSERT_TRUE(reader.open(testFileName, CbfMode::Read));

        CbfSectionHeader header;

        // Read Header Section
        ASSERT_TRUE(reader.readSectionHeader(header));
        EXPECT_EQ(header.type, static_cast<uint8_t>(CbfSectionType::Header));
        EXPECT_STREQ(header.name, "Engine_1");

        RecRule readRule;
        ASSERT_TRUE(reader.readRule(header, readRule));
        ASSERT_EQ(readRule.getAttributes().size(), 3);
        EXPECT_EQ(readRule.getType(sensorValueId), DataType::dtInt32);
        EXPECT_EQ(readRule.getType(voltageId), DataType::dtDouble);

        // Read Data Section
        ASSERT_TRUE(reader.readSectionHeader(header));
        EXPECT_EQ(header.type, static_cast<uint8_t>(CbfSectionType::Data));
        EXPECT_STREQ(header.name, "Engine_1");

        std::vector<uint8_t> readBuffer(readRule.getRecSize());
        Record rec(readRule, readBuffer.data());

        size_t recordSize = readRule.getRecSize();
        ASSERT_GT(recordSize, 0);
        size_t recordsCount = header.bodyLength / recordSize;
        EXPECT_EQ(recordsCount, 2);

        // Verify Record 1
        ASSERT_TRUE(reader.readRecord(rec));
        EXPECT_DOUBLE_EQ(rec.getValue(tsId), 123456789.0);
        EXPECT_EQ(static_cast<int32_t>(rec.getValue(sensorValueId)), 42);
        EXPECT_DOUBLE_EQ(rec.getValue(voltageId), 3.14);

        // Verify Record 2
        ASSERT_TRUE(reader.readRecord(rec));
        EXPECT_DOUBLE_EQ(rec.getValue(tsId), 123456790.0);
        EXPECT_EQ(static_cast<int32_t>(rec.getValue(sensorValueId)), 100);
        EXPECT_DOUBLE_EQ(rec.getValue(voltageId), 5.55);

        reader.close();
    }
}

TEST_F(CbfFileTest, OpenInvalidFile) {
    CbfFile reader;
    EXPECT_FALSE(reader.open("non_existent_file_12345.cbf", CbfMode::Read));
}

// =========================================================================
// CBF READER TESTS
// =========================================================================

class CbfReaderTest : public ::testing::Test {
protected:
    std::string testFileName = "test_data_reader.cbf";

    void SetUp() override {
        std::remove(testFileName.c_str());
    }

    void TearDown() override {
        std::remove(testFileName.c_str());
    }

    void createTestFile(int recordCount) {
        std::vector<PAttr> attrs;
        attrs.emplace_back("ValueInt", DataType::dtInt32);
        attrs.emplace_back("ValueDbl", DataType::dtDouble);
        RecRule rule(attrs);

        auto buffer = std::make_shared<RecBuffer>(rule, 1000);
        CbfWriter fileWriter(testFileName, buffer, true);
        fileWriter.setAlias("TestGen");

        RecordWriter sourceWriter(buffer, 100);

        const auto& rulesAttrs = rule.getAttributes();
        int idInt = rulesAttrs[1].id;
        int idDbl = rulesAttrs[2].id;

        for (int i = 0; i < recordCount; ++i) {
            Record rec = sourceWriter.nextRecord();
            rec.setInt32(idInt, i);
            rec.setDouble(idDbl, i * 1.5);
            sourceWriter.commitRecord();
        }

        sourceWriter.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        fileWriter.finish();
    }
};

TEST_F(CbfReaderTest, ReadValidFileEndToEnd) {
    const int recordCount = 500;
    createTestFile(recordCount);

    CbfReader reader(testFileName, 2000, true, 50);
    reader.join();

    auto buffer = reader.getBuffer();
    ASSERT_NE(buffer, nullptr) << "Buffer should be created after initialization";

    RecRule rule = buffer->getRule();
    ASSERT_EQ(rule.getAttributes().size(), 3);
    EXPECT_EQ(buffer->getTotalWritten(), recordCount);

    size_t recSize = buffer->getRecSize();
    std::vector<uint8_t> rawData(recSize);
    Record checkRec(rule, rawData.data());

    int idInt = -1;
    int idDbl = -1;

    for (const auto& attr : rule.getAttributes()) {
        if (strcmp(attr.name, "ValueInt") == 0) idInt = attr.id;
        if (strcmp(attr.name, "ValueDbl") == 0) idDbl = attr.id;
    }

    ASSERT_NE(idInt, -1);
    ASSERT_NE(idDbl, -1);

    // Verify 10th record
    buffer->readRelative(10, rawData.data(), 1);
    EXPECT_EQ(checkRec.getInt32(idInt), 10);
    EXPECT_DOUBLE_EQ(checkRec.getDouble(idDbl), 15.0);

    // Verify last record
    buffer->readRelative(recordCount - 1, rawData.data(), 1);
    EXPECT_EQ(checkRec.getInt32(idInt), recordCount - 1);
    EXPECT_DOUBLE_EQ(checkRec.getDouble(idDbl), (recordCount - 1) * 1.5);
}

TEST_F(CbfReaderTest, HandlesMissingFileGracefully) {
    CbfReader reader("non_existent_file_123.cbf", 1000, true);
    reader.join();

    EXPECT_FALSE(reader.isValid());
    auto buffer = reader.getBuffer();
    ASSERT_EQ(buffer, nullptr);
}

TEST_F(CbfReaderTest, ReadEmptyDataSection) {
    {
        std::vector<PAttr> attrs = {{"Val", DataType::dtInt32}};
        RecRule rule(attrs);
        auto buffer = std::make_shared<RecBuffer>(rule, 100);
        CbfWriter writer(testFileName, buffer, true);
        writer.finish(); // Write header only, no data
    }

    CbfReader reader(testFileName);
    reader.join();

    auto buffer = reader.getBuffer();
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->getTotalWritten(), 0);
}

// =========================================================================
// CBF WRITER INTEGRATION TESTS
// =========================================================================

class CbfWriterIntegrationTest : public ::testing::Test {
protected:
    std::string testFileName = "test_data_writer.cbf";

    void SetUp() override {
        std::remove(testFileName.c_str());
    }

    void TearDown() override {
        std::remove(testFileName.c_str());
    }
};

TEST_F(CbfWriterIntegrationTest, ProducerConsumerCycle) {
    std::vector<PAttr> attrs;
    attrs.emplace_back("Counter", DataType::dtInt32);
    attrs.emplace_back("SineValue", DataType::dtDouble);
    RecRule rule(attrs);

    size_t bufferCapacity = 2000;
    auto buffer = std::make_shared<RecBuffer>(rule, bufferCapacity);

    cyc::RecordWriter producer(buffer, 100);
    cyc::CbfWriter consumer(testFileName, buffer, true, 100);
    std::string alias = "IntgrTest";
    consumer.setAlias(alias);

    const int totalRecords = 5000;
    const auto& ruleAttrs = rule.getAttributes();
    int idTimeStamp = ruleAttrs[0].id;
    int idCounter   = ruleAttrs[1].id;
    int idSine      = ruleAttrs[2].id;

    for (int i = 0; i < totalRecords; ++i) {
        Record rec = producer.nextRecord();
        rec.setDouble(idTimeStamp, static_cast<double>(100000 + i));
        rec.setInt32(idCounter, i);
        rec.setDouble(idSine, static_cast<double>(i) * 0.5);
        producer.commitRecord();
    }

    producer.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    consumer.finish();

    ASSERT_FALSE(consumer.isRunning());

    // Validate using CbfFile
    CbfFile reader;
    ASSERT_TRUE(reader.open(testFileName, CbfMode::Read));

    CbfSectionHeader header;
    ASSERT_TRUE(reader.readSectionHeader(header));
    EXPECT_EQ(header.type, static_cast<uint8_t>(CbfSectionType::Header));
    EXPECT_STREQ(header.name, alias.c_str());

    RecRule readRule;
    ASSERT_TRUE(reader.readRule(header, readRule));

    ASSERT_TRUE(reader.readSectionHeader(header));
    EXPECT_EQ(header.type, static_cast<uint8_t>(CbfSectionType::Data));

    size_t expectedBytes = totalRecords * readRule.getRecSize();
    EXPECT_EQ(header.bodyLength, static_cast<int64_t>(expectedBytes));

    std::vector<uint8_t> readBuf(readRule.getRecSize());
    Record readRec(readRule, readBuf.data());

    int rIdTS = -1, rIdCnt = -1, rIdSine = -1;

    for(const auto& a : readRule.getAttributes()) {
        if (strcmp(a.name, "TimeStamp") == 0) rIdTS = a.id;
        if (strcmp(a.name, "Counter") == 0)   rIdCnt = a.id;
        if (strcmp(a.name, "SineValue") == 0) rIdSine = a.id;
    }
    ASSERT_NE(rIdTS, -1);
    ASSERT_NE(rIdCnt, -1);
    ASSERT_NE(rIdSine, -1);

    int count = 0;
    while (reader.readRecord(readRec)) {
        EXPECT_DOUBLE_EQ(readRec.getDouble(rIdTS), 100000.0 + count);
        EXPECT_EQ(readRec.getInt32(rIdCnt), count);
        EXPECT_DOUBLE_EQ(readRec.getDouble(rIdSine), count * 0.5);
        count++;
    }

    EXPECT_EQ(count, totalRecords);
}
