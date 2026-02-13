#include <gtest/gtest.h>
#include <cstdio>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

#include "Cbf/CbfWriter.h"
#include "Cbf/CbfFile.h"
#include "Core/RecBuffer.h"
#include "Core/RecRule.h"
#include "RecordWriter.h" // Используем RecordWriter как генератор
#include "Core/PAttr.h"

using namespace cyc;

class CbfWriterIntegrationTest : public ::testing::Test {
protected:
    std::string testFileName = "integration_test.cbf";

    void SetUp() override {
        // Очистка перед запуском
        std::remove(testFileName.c_str());
    }

    void TearDown() override {
        // Очистка после завершения
        std::remove(testFileName.c_str());
    }
};

TEST_F(CbfWriterIntegrationTest, ProducerConsumerCycle) {
    // 1. Определение схемы данных
    std::vector<PAttr> attrs;
    attrs.emplace_back("Counter", DataType::dtInt32);
    attrs.emplace_back("SineValue", DataType::dtDouble);
    RecRule rule(attrs);

    // 2. Создание буфера и участников
    size_t bufferCapacity = 2000;
    auto buffer = std::make_shared<RecBuffer>(rule, bufferCapacity);

    // Producer: RecordWriter (пишет в буфер)
    // batchCapacity = 100
    cyc::RecordWriter producer(buffer, 100);

    // Consumer: CbfWriter (читает из буфера и пишет в файл)
    // autoStart = true, batchSize = 100
    cyc::CbfWriter consumer(testFileName, buffer, true, 100);
    std::string alias = "IntgrTest";
    consumer.setAlias(alias);

    const int TotalRecords = 5000;

    // 3. Цикл генерации данных
    // Получаем индексы атрибутов из правила (TimeStamp всегда [0])
    const auto& ruleAttrs = rule.getAttributes();
    int idTimeStamp = ruleAttrs[0].id;
    int idCounter   = ruleAttrs[1].id;
    int idSine      = ruleAttrs[2].id;

    for (int i = 0; i < TotalRecords; ++i) {
        // Получаем доступ к памяти для следующей записи
        Record rec = producer.nextRecord();

        // Заполняем поля
        rec.setDouble(idTimeStamp, static_cast<double>(100000 + i)); // Детерминированный TimeStamp
        rec.setInt32(idCounter, i);
        rec.setDouble(idSine, static_cast<double>(i) * 0.5);

        // Фиксируем запись (сдвигает курсор, может вызвать swap буферов)
        producer.commitRecord();
    }

    // 4. Завершение работы
    // Принудительно сбрасываем остатки из локального буфера producer'а в RecBuffer
    producer.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // Даем consumer'у вычитать всё из RecBuffer и записать в файл
    consumer.finish();

    // Убеждаемся, что поток записи остановился
    ASSERT_FALSE(consumer.isRunning());

    // =========================================================================
    // 5. ПРОВЕРКА (Validation) через CbfFile
    // =========================================================================
    CbfFile reader;
    ASSERT_TRUE(reader.open(testFileName, CbfMode::Read));

    CbfSectionHeader header;

    // --- Проверка заголовка ---
    ASSERT_TRUE(reader.readSectionHeader(header));
    EXPECT_EQ(header.type, static_cast<uint8_t>(CbfSectionType::Header));
    EXPECT_STREQ(header.name, alias.c_str());

    RecRule readRule;
    ASSERT_TRUE(reader.readRule(header, readRule));

    // --- Проверка секции данных ---
    ASSERT_TRUE(reader.readSectionHeader(header));
    EXPECT_EQ(header.type, static_cast<uint8_t>(CbfSectionType::Data));

    // Проверяем общий размер данных
    size_t expectedBytes = TotalRecords * readRule.getRecSize();
    EXPECT_EQ(header.bodyLength, static_cast<int64_t>(expectedBytes));

    // --- Построчное чтение и сверка ---
    std::vector<uint8_t> readBuf(readRule.getRecSize());
    Record readRec(readRule, readBuf.data());

    // Ищем ID в прочитанном правиле по именам (на случай смены порядка)
    int rIdTS = -1, rIdCnt = -1, rIdSine = -1;
    for(const auto& a : readRule.getAttributes()) {
        std::string name = a.name;
        if (name == "TimeStamp") rIdTS = a.id;
        if (name == "Counter")   rIdCnt = a.id;
        if (name == "SineValue") rIdSine = a.id;
    }
    ASSERT_NE(rIdTS, -1);
    ASSERT_NE(rIdCnt, -1);
    ASSERT_NE(rIdSine, -1);

    int count = 0;
    while (reader.readRecord(readRec)) {
        // Сверяем значения
        EXPECT_DOUBLE_EQ(readRec.getDouble(rIdTS), 100000.0 + count);
        EXPECT_EQ(readRec.getInt32(rIdCnt), count);
        EXPECT_DOUBLE_EQ(readRec.getDouble(rIdSine), count * 0.5);
        count++;
    }

    EXPECT_EQ(count, TotalRecords);
}
