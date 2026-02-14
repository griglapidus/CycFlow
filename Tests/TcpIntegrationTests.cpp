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
    TcpDataReceiver receiver(1000); // 1000 records capacity

    // 1. Connect Receiver
    bool connected = receiver.connect(m_host, m_port, m_bufferName);
    ASSERT_TRUE(connected);

    // 2. Wait for connection and handshake (RecRule reception)
    // We poll until the buffer is created inside the receiver
    int retries = 0;
    while (!receiver.getBuffer() && retries < 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        retries++;
    }
    ASSERT_TRUE(receiver.getBuffer() != nullptr) << "Receiver failed to receive RecRule and init buffer";

    // 3. Push Data to Source
    const int recordCount = 50;
    for (int i = 0; i < recordCount; ++i) {
        pushTestData(static_cast<double>(i * 1.1), static_cast<double>(i));
    }

    // 4. Wait for data to arrive at Receiver
    // We expect totalWritten to match
    retries = 0;
    while (receiver.getBuffer()->getTotalWritten() < recordCount && retries < 40) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        retries++;
    }

    // 5. Verification
    auto destBuffer = receiver.getBuffer();
    EXPECT_EQ(destBuffer->getTotalWritten(), recordCount);

    // Read last record to verify content
    // Record size is 16 bytes.
    std::vector<uint8_t> readBuf(16);
    // Read relative index 0 (if buffer not wrapped) or calculate global
    // Let's just read the last one pushed (index 49)
    // NOTE: In a circular buffer, relative index changes, but here capacity(1000) > count(50)

    // We read strictly from the beginning (index 0 relative to current window)
    destBuffer->readRelative(0, readBuf.data(), 1);

    double ts, val;
    std::memcpy(&ts, readBuf.data(), 8);
    std::memcpy(&val, readBuf.data() + 8, 8);

    // Check first record (i=0)
    EXPECT_DOUBLE_EQ(ts, 0.0);
    EXPECT_DOUBLE_EQ(val, 0.0);

    // Check 10th record
    destBuffer->readRelative(10, readBuf.data(), 1);
    std::memcpy(&ts, readBuf.data(), 8);
    std::memcpy(&val, readBuf.data() + 8, 8);
    EXPECT_DOUBLE_EQ(ts, 10.0);
    EXPECT_DOUBLE_EQ(val, 11.0); // 10 * 1.1

    receiver.stop();
}

// =========================================================================
// TEST 3: Connection Errors
// =========================================================================

TEST_F(TcpIntegrationTest, ConnectToNonExistentBufferReturnsError) {
    // Attempting to stream a buffer that is not registered
    TcpDataReceiver receiver;
    bool connected = receiver.connect(m_host, m_port, "MissingBuffer");
    EXPECT_FALSE(connected); // Physical TCP connect works
}
