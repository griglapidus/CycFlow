// TcpTests.cpp
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include "Tcp/TcpServer.h"
#include "Tcp/TcpServiceClient.h"
#include "Core/RecBuffer.h"
#include "Core/RecRule.h"
#include "Core/PAttr.h"
#include "Tcp/TcpDefs.h"

#include <asio.hpp>
#include <thread>
#include <chrono>
#include <memory>
#include <algorithm>

using namespace cyc;

// Test fixture for setting up and tearing down the TCP server
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
        // 1. Prepare schema and buffers
        std::vector<PAttr> attrs;
        attrs.push_back(PAttr("SensorValue", DataType::dtDouble, 1));
        m_rule.init(attrs);

        m_bufferA = std::make_shared<RecBuffer>(m_rule, 100);
        m_bufferB = std::make_shared<RecBuffer>(m_rule, 100);

        // 2. Initialize and start the server
        m_server = std::make_unique<TcpServer>(m_ioContext, m_port);
        m_server->registerBuffer("BufferA", m_bufferA);
        m_server->registerBuffer("BufferB", m_bufferB);
        m_server->start();

        // 3. Run io_context in a background thread
        m_serverThread = std::thread([this]() {
            m_ioContext.run();
        });

        // Give the server a tiny fraction of time to bind and start listening
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void TearDown() override {
        // Stop the io_context and join the thread
        m_ioContext.stop();
        if (m_serverThread.joinable()) {
            m_serverThread.join();
        }
    }
};

// =========================================================================
// TESTS
// =========================================================================

TEST_F(TcpNetworkingTest, RequestBufferListReturnsAllRegisteredBuffers) {
    auto bufferList = TcpServiceClient::requestBufferList("127.0.0.1", m_port);

    ASSERT_EQ(bufferList.size(), 2);

    // Check if both buffers are in the list (order is not guaranteed due to unordered_map)
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
