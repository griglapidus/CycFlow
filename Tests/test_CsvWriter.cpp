#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <synchapi.h>
#include <vector>
#include <cstdio>
#include <sstream>
#include "CsvWriter.h"
#include "RecBuffer.h"
#include "RecRule.h"
#include "Record.h"
#include "PAttr.h"
#include "RecordWriter.h"

using namespace cyc;

class CsvWriterTest : public ::testing::TestWithParam<int> {
protected:
    std::string filename = "test_output.csv";

    // Removes the output file before the test runs to ensure a clean state.
    void SetUp() override {
        std::remove(filename.c_str());
    }

    // Removes the output file after the test finishes to clean up resources.
    void TearDown() override {
        std::remove(filename.c_str());
    }

    // Helper function to read the file content line by line into a vector for verification.
    std::vector<std::string> readLines(const std::string& fname) {
        std::ifstream file(fname);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty()) lines.push_back(line);
        }
        return lines;
    }

    // Helper function to construct a raw binary record from given values for testing.
    std::vector<uint8_t> createRawRecord(const RecRule& rule, int idVal, double dblVal, const std::string& strVal) {
        std::vector<uint8_t> raw(rule.getRecSize(), 0);
        Record rec(rule, raw.data());

        if (rule.getAttributes().size() > 0) rec.setDouble(rule.getAttributes()[0].id, 1.0);
        if (rule.getAttributes().size() > 1) rec.setInt32(rule.getAttributes()[1].id, idVal);
        if (rule.getAttributes().size() > 2) rec.setDouble(rule.getAttributes()[2].id, dblVal);

        if(rule.getAttributes().size() > 3) {
            char* dest = rec.getCharPtr(rule.getAttributes()[3].id);
            size_t count = std::min(strVal.size(), 9ull);
            std::copy_n(strVal.begin(), count, dest);
        }
        return raw;
    }
};

// Verifies that the CSV file is created with the correct header row based on attributes.
TEST_F(CsvWriterTest, CreatesFileWithHeader) {
    std::vector<PAttr> attrs;
    attrs.emplace_back("ColInt", DataType::dtInt32);
    attrs.emplace_back("ColDbl", DataType::dtDouble);
    RecRule rule(attrs);
    auto buffer = std::make_shared<RecBuffer>(rule, 100);

    {
        CsvWriter writer(filename, buffer);
    }

    auto lines = readLines(filename);
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0], "TimeStamp,ColInt,ColDbl");
}

// Tests writing records with various data types and verifies the CSV formatting (precision, quotes).
TEST_F(CsvWriterTest, WritesFormattedData) {
    std::vector<PAttr> attrs;
    attrs.emplace_back("ID", DataType::dtInt32);
    attrs.emplace_back("Value", DataType::dtDouble);
    attrs.emplace_back("Name", DataType::dtChar, 10);
    RecRule rule(attrs);

    auto buffer = std::make_shared<RecBuffer>(rule, 100);
    std::vector<uint8_t> rawBatch;

    auto r1 = createRawRecord(rule, 10, 3.14, "TestA");
    rawBatch.insert(rawBatch.end(), r1.begin(), r1.end());

    auto r2 = createRawRecord(rule, 20, 0.005, "TestB");
    rawBatch.insert(rawBatch.end(), r2.begin(), r2.end());

    CsvWriter writer(filename, buffer);
    buffer->push(rawBatch.data(), 2);

    writer.finish();

    auto lines = readLines(filename);
    ASSERT_EQ(lines.size(), 3);
    EXPECT_EQ(lines[1], "1.000000,10,3.140000,\"TestA\"");
    EXPECT_EQ(lines[2], "1.000000,20,0.005000,\"TestB\"");
}

// Ensures that new records are appended to an existing file rather than overwriting it.
TEST_F(CsvWriterTest, AppendsToExistingFile) {
    {
        std::ofstream f(filename);
        f << "TimeStamp,ID,Value\n";
        f << "1.000000,1,1.1\n";
    }

    std::vector<PAttr> attrs;
    attrs.emplace_back("ID", DataType::dtInt32);
    attrs.emplace_back("Value", DataType::dtDouble);
    RecRule rule(attrs);
    auto buffer = std::make_shared<RecBuffer>(rule, 100);

    CsvWriter writer(filename, buffer);
    auto raw = createRawRecord(rule, 2, 2.2, "");
    buffer->push(raw.data(), 1);

    writer.finish();

    auto lines = readLines(filename);
    ASSERT_EQ(lines.size(), 3);
    EXPECT_EQ(lines[2], "1.000000,2,2.200000");
}

// Checks if the writer can handle a large batch of records correctly.
TEST_F(CsvWriterTest, HandlesManyRecords) {
    std::vector<PAttr> attrs = { PAttr("X", DataType::dtInt32) };
    RecRule rule(attrs);
    auto buffer = std::make_shared<RecBuffer>(rule, 5000);

    CsvWriter writer(filename, buffer);
    const int COUNT = 1000;
    std::vector<uint8_t> hugeBatch;
    hugeBatch.reserve(COUNT * rule.getRecSize());

    for(int i=0; i<COUNT; ++i) {
        std::vector<uint8_t> raw(rule.getRecSize());
        Record r(rule, raw.data());
        if(rule.getAttributes().size() > 0) r.setDouble(rule.getAttributes()[0].id, 1);
        if(rule.getAttributes().size() > 1) r.setInt32(rule.getAttributes()[1].id, i);
        hugeBatch.insert(hugeBatch.end(), raw.begin(), raw.end());
    }

    buffer->push(hugeBatch.data(), COUNT);
    writer.finish();

    auto lines = readLines(filename);
    EXPECT_EQ(lines.size(), COUNT + 1);
    EXPECT_EQ(lines.back(), "1.000000,999");
}

// Simulates a production scenario with RecordWriter and verifies the integrity of written data.
TEST_P(CsvWriterTest, LongRunningProducerConsumer) {
    std::vector<PAttr> attrs;
    attrs.emplace_back("Counter", DataType::dtInt32);
    RecRule rule(attrs);
    auto buffer = std::make_shared<RecBuffer>(rule, 2000);
    cyc::RecordWriter writer(buffer, 100);
    cyc::CsvWriter csvWriter(filename, buffer, true, 100);

    const int totalRecords = 5000;

    for (int i = 0; i < totalRecords; ++i) {
        Record rec = writer.nextRecord();

        rec.setDouble(rule.getAttributes()[0].id, cyc::get_current_epoch_time());
        rec.setInt32(rule.getAttributes()[1].id, i);

        writer.commitRecord();
    }
    writer.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    csvWriter.finish();

    auto lines = readLines(filename);

    EXPECT_EQ(lines.size(), totalRecords + 1)
        << "Expected " << totalRecords + 1 << " lines, got " << lines.size();

    if (!lines.empty()) {
        std::string lastLine = lines.back();
        std::stringstream ss(lastLine);
        std::string segment;
        std::vector<std::string> parts;
        while(std::getline(ss, segment, ',')) {
            parts.push_back(segment);
        }

        ASSERT_GE(parts.size(), 2);
        try {
            int lastIndex = std::stoi(parts[1]);
            EXPECT_EQ(lastIndex, totalRecords - 1);
        } catch (...) {
            FAIL() << "Could not parse last index from: " << lastLine;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    StandardSuite,
    CsvWriterTest,
    ::testing::Range(0, 5)
    );
