// test_Tcp.cpp
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <asio.hpp>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include <cstring>
#include <algorithm>

#include "Tcp/TcpServer.h"
#include "Tcp/TcpServiceClient.h"
#include "Tcp/TcpDataReceiver.h"
#include "Core/RecBuffer.h"
#include "Core/RecRule.h"
#include "Core/PAttr.h"

using namespace cyc;

// =========================================================================
// TCP NETWORKING BASIC TESTS
// =========================================================================

class TcpNetworkingTest : public ::testing::Test {
protected:
    uint16_t m_port = 15555;
    asio::io_context m_ioContext;
    std::unique_ptr<TcpServer> m_server;
    std::thread m_serverThread;

    std::shared_ptr<RecBuffer> m_bufferA;
    std::shared_ptr<RecBuffer> m_bufferB;
    RecRule m_rule;

    void SetUp() override {
        std::vector<PAttr> attrs;
        attrs.push_back(PAttr("SensorValue", DataType::dtDouble, 1));
        m_rule.init(attrs);

        m_bufferA = std::make_shared<RecBuffer>(m_rule, 100);
        m_bufferB = std::make_shared<RecBuffer>(m_rule, 100);

        m_server = std::make_unique<TcpServer>(m_ioContext, m_port);
        m_server->registerBuffer("BufferA", m_bufferA);
        m_server->registerBuffer("BufferB", m_bufferB);
        m_server->start();

        m_serverThread = std::thread([this]() {
            m_ioContext.run();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void TearDown() override {
        m_ioContext.stop();
        if (m_serverThread.joinable()) {
            m_serverThread.join();
        }
    }
};

TEST_F(TcpNetworkingTest, RequestBufferListReturnsAllRegisteredBuffers) {
    auto bufferList = TcpServiceClient::requestBufferList("127.0.0.1", m_port);
    ASSERT_EQ(bufferList.size(), 2);

    bool hasBufferA = std::find(bufferList.begin(), bufferList.end(), "BufferA") != bufferList.end();
    bool hasBufferB = std::find(bufferList.begin(), bufferList.end(), "BufferB") != bufferList.end();

    EXPECT_TRUE(hasBufferA);
    EXPECT_TRUE(hasBufferB);
}

TEST_F(TcpNetworkingTest, RequestRecRuleReturnsCorrectTextForExistingBuffer) {
    std::string ruleText = TcpServiceClient::requestRecRule("127.0.0.1", m_port, "BufferA");
    EXPECT_FALSE(ruleText.empty());
    EXPECT_EQ(ruleText, m_rule.toText());
}

TEST_F(TcpNetworkingTest, RequestRecRuleReturnsEmptyStringForUnknownBuffer) {
    std::string ruleText = TcpServiceClient::requestRecRule("127.0.0.1", m_port, "UnknownBuffer");
    EXPECT_TRUE(ruleText.empty());
}

// =========================================================================
// TCP INTEGRATION TESTS
// =========================================================================

RecRule createTestRule() {
    std::vector<PAttr> attrs;
    attrs.emplace_back("Value", DataType::dtDouble, 1);
    RecRule rule;
    rule.init(attrs);
    return rule;
}

class TcpIntegrationTest : public ::testing::Test {
protected:
    const uint16_t m_port = 15556; // Use different port to avoid conflicts
    const std::string m_host = "127.0.0.1";
    const std::string m_bufferName = "TestStream";

    asio::io_context m_ioContext;
    std::unique_ptr<TcpServer> m_server;
    std::thread m_serverThread;

    std::shared_ptr<RecBuffer> m_sourceBuffer;
    RecRule m_rule;

    void SetUp() override {
        m_rule = createTestRule();
        m_sourceBuffer = std::make_shared<RecBuffer>(m_rule, 1000);

        m_server = std::make_unique<TcpServer>(m_ioContext, m_port);
        m_server->registerBuffer(m_bufferName, m_sourceBuffer);
        m_server->start();

        m_serverThread = std::thread([this]() {
            auto workGuard = asio::make_work_guard(m_ioContext);
            m_ioContext.run();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void TearDown() override {
        m_ioContext.stop();
        if (m_serverThread.joinable()) {
            m_serverThread.join();
        }
    }

    void pushTestData(double value, double timestamp = 1.0) {
        std::vector<uint8_t> rawData(16);
        std::memcpy(rawData.data(), &timestamp, 8);
        std::memcpy(rawData.data() + 8, &value, 8);
        m_sourceBuffer->push(rawData.data(), 1);
    }
};

TEST_F(TcpIntegrationTest, ClientCanDiscoverBuffers) {
    auto list = TcpServiceClient::requestBufferList(m_host, m_port);
    ASSERT_EQ(list.size(), 1);
    EXPECT_EQ(list[0], m_bufferName);
}

TEST_F(TcpIntegrationTest, ClientCanRetrieveRecRule) {
    std::string receivedRule = TcpServiceClient::requestRecRule(m_host, m_port, m_bufferName);
    EXPECT_FALSE(receivedRule.empty());

    RecRule localRule = RecRule::fromText(receivedRule);
    EXPECT_EQ(localRule.getRecSize(), m_rule.getRecSize());
    EXPECT_EQ(localRule.getAttributes().size(), 2);
    EXPECT_EQ(std::string(localRule.getAttributes()[1].name), "Value");
}

TEST_F(TcpIntegrationTest, ReceiverGetsStreamedData) {
    TcpDataReceiver receiver(1000, 20);

    bool connected = receiver.connect(m_host, m_port, m_bufferName);
    ASSERT_TRUE(connected);
    ASSERT_TRUE(receiver.getBuffer() != nullptr);

    const int recordCount = 50;
    for (int i = 0; i < recordCount; ++i) {
        pushTestData(static_cast<double>(i * 1.1), static_cast<double>(i));
    }

    int retries = 0;
    auto destBuffer = receiver.getBuffer();

    while (destBuffer->getTotalWritten() < recordCount && retries < 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        retries++;
    }

    ASSERT_EQ(destBuffer->getTotalWritten(), recordCount) << "Buffer is missing data";

    std::vector<uint8_t> readBuf(16);
    destBuffer->readRelative(0, readBuf.data(), 1);

    double val;
    std::memcpy(&val, readBuf.data() + 8, 8);
    EXPECT_DOUBLE_EQ(val, 0.0);

    destBuffer->readRelative(10, readBuf.data(), 1);
    std::memcpy(&val, readBuf.data() + 8, 8);
    EXPECT_DOUBLE_EQ(val, 11.0);

    receiver.stop();
}

TEST_F(TcpIntegrationTest, ConnectToNonExistentBufferReturnsError) {
    TcpDataReceiver receiver(1000);
    bool connected = receiver.connect(m_host, m_port, "MissingBuffer");

    EXPECT_FALSE(connected);
    EXPECT_EQ(receiver.getBuffer(), nullptr);
}
