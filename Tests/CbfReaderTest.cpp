#include <gtest/gtest.h>
#include <cstdio>
#include <vector>
#include <string>

#include "Cbf/CbfReader.h"
#include "Cbf/CbfWriter.h"
#include "Core/RecBuffer.h"
#include "Core/RecRule.h"
#include "Core/Record.h"
#include "Core/PAttr.h"
#include "RecordWriter.h" // Нужен для генерации данных

using namespace cyc;

class CbfReaderTest : public ::testing::Test {
protected:
    std::string testFileName = "reader_test_data.cbf";

    void SetUp() override {
        // Убедимся, что файла нет перед тестом
        std::remove(testFileName.c_str());
    }

    void TearDown() override {
        // Удаляем файл после теста
        std::remove(testFileName.c_str());
    }

    // Вспомогательная функция для создания валидного CBF файла
    void createTestFile(int recordCount) {
        // 1. Схема: TimeStamp (авто) + ValueInt + ValueDbl
        std::vector<PAttr> attrs;
        attrs.emplace_back("ValueInt", DataType::dtInt32);
        attrs.emplace_back("ValueDbl", DataType::dtDouble);
        RecRule rule(attrs);

        // 2. Создаем буфер и Writer
        auto buffer = std::make_shared<RecBuffer>(rule, 1000);

        // Writer пишет в файл. autoStart=true
        CbfWriter fileWriter(testFileName, buffer, true);
        fileWriter.setAlias("TestGen");

        // 3. Используем RecordWriter как источник данных
        RecordWriter sourceWriter(buffer, 100);

        // ID полей (0 - TimeStamp, 1 - Int, 2 - Double)
        // Но лучше искать по имени или порядку, так как RecRule::init добавляет TimeStamp первым
        const auto& rulesAttrs = rule.getAttributes();
        int idInt = rulesAttrs[1].id;
        int idDbl = rulesAttrs[2].id;

        for (int i = 0; i < recordCount; ++i) {
            Record rec = sourceWriter.nextRecord();

            rec.setInt32(idInt, i);
            rec.setDouble(idDbl, i * 1.5);

            sourceWriter.commitRecord();
        }

        // Сбрасываем данные в буфер
        sourceWriter.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        // Ждем, пока CbfWriter запишет всё в файл и закроет его
        fileWriter.finish();
    }
};

TEST_F(CbfReaderTest, ReadValidFileEndToEnd) {
    const int RecordCount = 500;

    // 1. Генерируем тестовый файл
    createTestFile(RecordCount);

    // 2. Создаем Reader
    // bufferCapacity: емкость создаваемого буфера
    // autoStart: true (сразу запускает поток)
    // readBatchSize: 50 (читает с диска по 50 записей за раз)
    // writerBatchSize: 10 (пишет в RecBuffer по 10)
    CbfReader reader(testFileName, 2000, true, 50, 10);
    // 3. Ждем завершения чтения
    // Так как это файл, reader остановится сам, когда дойдет до EOF
    reader.join();

    // 4. Проверяем результаты
    // Получаем буфер, который создал Reader
    auto buffer = reader.getBuffer();
    ASSERT_NE(buffer, nullptr) << "Buffer should be created after initialization";

    // Проверяем схему (TimeStamp + 2 поля)
    RecRule rule = buffer->getRule();
    ASSERT_EQ(rule.getAttributes().size(), 3);

    // Проверяем общее количество записей
    EXPECT_EQ(buffer->getTotalWritten(), RecordCount);

    // 5. Проверяем данные выборочно (читаем прямо из буфера)
    // Для проверки используем RecordReader (или низкоуровневый readFromGlobal)
    // Но для простоты теста используем readRelative (так как буфер большой и хранит всё)

    size_t recSize = buffer->getRecSize();
    std::vector<uint8_t> rawData(recSize);
    Record checkRec(rule, rawData.data());

    int idInt = -1;
    int idDbl = -1;

    // Ищем ID в восстановленном правиле
    for (const auto& attr : rule.getAttributes()) {
        std::string name = attr.name;
        if (name == "ValueInt") idInt = attr.id;
        if (name == "ValueDbl") idDbl = attr.id;
    }
    ASSERT_NE(idInt, -1);
    ASSERT_NE(idDbl, -1);

    // Проверяем 10-ю запись
    buffer->readRelative(10, rawData.data(), 1);
    EXPECT_EQ(checkRec.getInt32(idInt), 10);
    EXPECT_DOUBLE_EQ(checkRec.getDouble(idDbl), 15.0); // 10 * 1.5

    // Проверяем последнюю запись
    buffer->readRelative(RecordCount - 1, rawData.data(), 1);
    EXPECT_EQ(checkRec.getInt32(idInt), RecordCount - 1);
    EXPECT_DOUBLE_EQ(checkRec.getDouble(idDbl), (RecordCount - 1) * 1.5);
}

TEST_F(CbfReaderTest, HandlesMissingFileGracefully) {
    // 1. Создаем Reader для несуществующего файла
    // Конструктор не должен кидать исключений
    CbfReader reader("non_existent_file_123.cbf", 1000, true);

    // 2. Ждем завершения (поток должен стартовать и почти сразу остановиться)
    reader.join();

    // 3. Проверяем состояние
    EXPECT_FALSE(reader.isValid());

    // 4. Проверяем, что буфер пуст (или содержит 0 записей)
    auto buffer = reader.getBuffer();
    ASSERT_EQ(buffer, nullptr);
}

TEST_F(CbfReaderTest, ReadEmptyDataSection) {
    // Создаем файл только с заголовком (без записей данных)
    {
        std::vector<PAttr> attrs = {{"Val", DataType::dtInt32}};
        RecRule rule(attrs);
        auto buffer = std::make_shared<RecBuffer>(rule, 100);
        CbfWriter writer(testFileName, buffer, true);
        // Не пишем данных, просто закрываем
        writer.finish();
    }

    CbfReader reader(testFileName);
    reader.join();

    auto buffer = reader.getBuffer();
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->getTotalWritten(), 0);
}
