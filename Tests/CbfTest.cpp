#include <gtest/gtest.h>
#include <cstdio>
#include <vector>
#include <string>

#include "CbfFile.h"
#include "RecRule.h"
#include "Record.h"
#include "PAttr.h"

using namespace cyc;

class CbfFileTest : public ::testing::Test {
protected:
    std::string testFileName = "test_data.cbf";

    void TearDown() override {
        // Удаляем файл после теста, чтобы не мусорить
        std::remove(testFileName.c_str());
    }
};

TEST_F(CbfFileTest, WriteAndReadCycle) {
    // =========================================================================
    // 1. ПОДГОТОВКА СХЕМЫ (RULE)
    // =========================================================================
    // Обратите внимание: RecRule автоматически добавит TimeStamp (ID=1) первым полем.
    // Поэтому используем ID 10 и 11 для наших полей.
    std::vector<PAttr> attrs = {{"SensorValue", DataType::dtInt32, 1},
                                {"Voltage", DataType::dtDouble, 1}
    };
    RecRule writeRule(attrs);
    auto TSId = writeRule.getAttributes()[0].id;
    auto SensorValueId = writeRule.getAttributes()[1].id;
    auto VoltageId = writeRule.getAttributes()[2].id;
    // =========================================================================
    // 2. ЗАПИСЬ (WRITE)
    // =========================================================================
    {
        CbfFile writer;
        ASSERT_TRUE(writer.open(testFileName, CbfMode::Write));
        ASSERT_TRUE(writer.isOpen());

        // Устанавливаем псевдоним
        std::string myAlias = "Engine_1";
        writer.setAlias(myAlias);

        // Пишем заголовок (Схему)
        ASSERT_TRUE(writer.writeHeader(writeRule));

        // Начинаем секцию данных
        ASSERT_TRUE(writer.beginDataSection());

        // Подготавливаем буфер для записи
        std::vector<uint8_t> buffer(writeRule.getRecSize());
        Record rec(writeRule, buffer.data());

        // --- Запись 1 ---
        rec.clear();
        rec.setValue(TSId, 123456789.0); // TimeStamp (ID=1)
        rec.setValue(SensorValueId, 42.0);       // SensorValue
        rec.setValue(VoltageId, 3.14);       // Voltage
        ASSERT_TRUE(writer.writeRecord(rec));

        // --- Запись 2 ---
        rec.clear();
        rec.setValue(TSId, 123456790.0);
        rec.setValue(SensorValueId, 100.0);
        rec.setValue(VoltageId, 5.55);
        ASSERT_TRUE(writer.writeRecord(rec));

        // Завершаем секцию
        ASSERT_TRUE(writer.endDataSection());

        writer.close();
    }

    // =========================================================================
    // 3. ЧТЕНИЕ (READ)
    // =========================================================================
    {
        CbfFile reader;
        ASSERT_TRUE(reader.open(testFileName, CbfMode::Read));

        CbfSectionHeader header;

        // --- Читаем 1-ю секцию (Header) ---
        ASSERT_TRUE(reader.readSectionHeader(header));
        EXPECT_EQ(header.type, static_cast<uint8_t>(CbfSectionType::Header));
        EXPECT_STREQ(header.name, "Engine_1"); // Проверка Alias

        RecRule readRule;
        ASSERT_TRUE(reader.readRule(header, readRule));

        // Проверяем, что схема восстановилась корректно
        // TimeStamp + 2 наших поля = 3
        ASSERT_EQ(readRule.getAttributes().size(), 3);
        EXPECT_EQ(readRule.getType(SensorValueId), DataType::dtInt32);
        EXPECT_EQ(readRule.getType(VoltageId), DataType::dtDouble);

        // --- Читаем 2-ю секцию (Data) ---
        ASSERT_TRUE(reader.readSectionHeader(header));
        EXPECT_EQ(header.type, static_cast<uint8_t>(CbfSectionType::Data));
        EXPECT_STREQ(header.name, "Engine_1"); // Проверка Alias

        // Подготавливаем буфер для чтения (используем прочитанное правило!)
        std::vector<uint8_t> readBuffer(readRule.getRecSize());
        Record rec(readRule, readBuffer.data());

        // Расчет количества записей в секции
        size_t recordSize = readRule.getRecSize();
        ASSERT_GT(recordSize, 0);
        size_t recordsCount = header.bodyLength / recordSize;
        EXPECT_EQ(recordsCount, 2);

        // --- Читаем Запись 1 ---
        ASSERT_TRUE(reader.readRecord(rec));
        EXPECT_DOUBLE_EQ(rec.getValue(TSId), 123456789.0);
        EXPECT_EQ(static_cast<int32_t>(rec.getValue(SensorValueId)), 42);
        EXPECT_DOUBLE_EQ(rec.getValue(VoltageId), 3.14);

        // --- Читаем Запись 2 ---
        ASSERT_TRUE(reader.readRecord(rec));
        EXPECT_DOUBLE_EQ(rec.getValue(TSId), 123456790.0);
        EXPECT_EQ(static_cast<int32_t>(rec.getValue(SensorValueId)), 100);
        EXPECT_DOUBLE_EQ(rec.getValue(VoltageId), 5.55);

        reader.close();
    }
}

// Тест на проверку открытия несуществующего файла
TEST_F(CbfFileTest, OpenInvalidFile) {
    CbfFile reader;
    EXPECT_FALSE(reader.open("non_existent_file_12345.cbf", CbfMode::Read));
}
