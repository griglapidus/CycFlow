// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include <gtest/gtest.h>
#include <asio.hpp>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include <cstring>

// Include your project headers
#include "Tcp/TcpServer.h"
#include "Tcp/TcpServiceClient.h"
#include "Tcp/TcpDataReceiver.h"
#include "Core/RecBuffer.h"
#include "Core/RecRule.h"
#include "Core/PAttr.h"

using namespace cyc;

// Helper to create a simple schema: [TimeStamp (Implicit), Value (Double)]
RecRule createTestRule() {
    std::vector<PAttr> attrs;
    attrs.emplace_back("Value", DataType::dtDouble, 1);
    RecRule rule;
    rule.init(attrs); // buildHeader() is called internally, adding TimeStamp
    return rule;
}

class TcpIntegrationTest : public ::testing::Test {
protected:
    // Constants
    const uint16_t m_port = 15555;
    const std::string m_host = "127.0.0.1";
    const std::string m_bufferName = "TestStream";

    // Members
    asio::io_context m_ioContext;
    std::unique_ptr<TcpServer> m_server;
    std::thread m_serverThread;

    std::shared_ptr<RecBuffer> m_sourceBuffer;
    RecRule m_rule;

    void SetUp() override {
        // 1. Prepare Data Source
        m_rule = createTestRule();
        m_sourceBuffer = std::make_shared<RecBuffer>(m_rule, 1000);

        // 2. Start Server
        m_server = std::make_unique<TcpServer>(m_ioContext, m_port);
        m_server->registerBuffer(m_bufferName, m_sourceBuffer);
        m_server->start();

        // 3. Run IO context in background
        m_serverThread = std::thread([this]() {
            // Prevent run() from exiting immediately if no work
            auto workGuard = asio::make_work_guard(m_ioContext);
            m_ioContext.run();
        });

        // Give server a moment to bind
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void TearDown() override {
        m_ioContext.stop();
        if (m_serverThread.joinable()) {
            m_serverThread.join();
        }
    }

    // Helper to push raw data (Timestamp + Value) to source buffer
    void pushTestData(double value, double timestamp = 1.0) {
        // Record structure: [TimeStamp (8 bytes)][Value (8 bytes)]
        std::vector<uint8_t> rawData(16);
        std::memcpy(rawData.data(), &timestamp, 8);
        std::memcpy(rawData.data() + 8, &value, 8);

        m_sourceBuffer->push(rawData.data(), 1);
    }
};

// =========================================================================
// TEST 1: Service Client Discovery
// =========================================================================

TEST_F(TcpIntegrationTest, ClientCanDiscoverBuffers) {
    auto list = TcpServiceClient::requestBufferList(m_host, m_port);

    ASSERT_EQ(list.size(), 1);
    EXPECT_EQ(list[0], m_bufferName);
}

TEST_F(TcpIntegrationTest, ClientCanRetrieveRecRule) {
    std::string receivedRule = TcpServiceClient::requestRecRule(m_host, m_port, m_bufferName);

    EXPECT_FALSE(receivedRule.empty());

    // Compare normalized text or reconstruct rule
    RecRule localRule = RecRule::fromText(receivedRule);
    EXPECT_EQ(localRule.getRecSize(), m_rule.getRecSize());

    // Check custom attribute existence
    EXPECT_EQ(localRule.getAttributes().size(), 2); // TimeStamp + Value
    EXPECT_EQ(std::string(localRule.getAttributes()[1].name), "Value");
}

// =========================================================================
// TEST 2: Data Streaming
// =========================================================================

TEST_F(TcpIntegrationTest, ReceiverGetsStreamedData) {
    // Создаем Receiver с емкостью буфера 1000 и размером батча записи 100
    TcpDataReceiver receiver(1000, 100);

    // 1. Connect Receiver
    // Теперь connect выполняет синхронный handshake. Если вернул true — буфер уже создан.
    bool connected = receiver.connect(m_host, m_port, m_bufferName);
    ASSERT_TRUE(connected);

    // 2. Check Buffer Existence
    // В новой реализации RecordProducer::initialize вызывается внутри connect/start
    ASSERT_TRUE(receiver.getBuffer() != nullptr) << "Receiver buffer should be initialized after successful connect";

    // 3. Push Data to Source
    const int recordCount = 50;
    for (int i = 0; i < recordCount; ++i) {
        pushTestData(static_cast<double>(i * 1.1), static_cast<double>(i));
        if(i == 25) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    receiver.stop();
    // 4. Wait for data to arrive at Receiver
    // Данные идут асинхронно, поэтому ждем заполнения
    int retries = 0;
    auto destBuffer = receiver.getBuffer();

    while (destBuffer->getTotalWritten() < recordCount && retries < 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        retries++;
    }

    // 5. Verification
    EXPECT_EQ(destBuffer->getTotalWritten(), recordCount);

    // Read last record to verify content
    // Record size is 16 bytes.
    std::vector<uint8_t> readBuf(16);

    // We read strictly from the beginning (index 0 relative to current window)
    destBuffer->readRelative(0, readBuf.data(), 1);

    double val;
    std::memcpy(&val, readBuf.data() + 8, 8);

    // Check first record (i=0)
    EXPECT_DOUBLE_EQ(val, 0.0);

    // Check 10th record
    destBuffer->readRelative(10, readBuf.data(), 1);
    std::memcpy(&val, readBuf.data() + 8, 8);
    EXPECT_DOUBLE_EQ(val, 11.0); // 10 * 1.1

    receiver.stop();
}

// =========================================================================
// TEST 3: Connection Errors
// =========================================================================

TEST_F(TcpIntegrationTest, ConnectToNonExistentBufferReturnsError) {
    // Attempting to stream a buffer that is not registered
    TcpDataReceiver receiver(1000);

    // connect должен вернуть false, так как сервер пришлет ResponseError во время handshake
    bool connected = receiver.connect(m_host, m_port, "MissingBuffer");

    EXPECT_FALSE(connected);
    EXPECT_EQ(receiver.getBuffer(), nullptr); // Буфер не должен быть создан
}
