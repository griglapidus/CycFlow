// TcpDataReceiver.h
// SPDX-License-Identifier: MIT

#ifndef CYC_TCPDATARECEIVER_H
#define CYC_TCPDATARECEIVER_H

#include "Core/CycLib_global.h"
#include <asio.hpp>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>

#include "Core/RecBuffer.h"

namespace cyc {

class CYCLIB_EXPORT TcpDataReceiver {
public:
    /**
     * @brief Creates a receiver.
     * @param bufferCapacity Capacity for the dynamically created RecBuffer.
     */
    TcpDataReceiver(size_t bufferCapacity = 1000);
    ~TcpDataReceiver();

    /**
     * @brief Connects to server and performs a handshake.
     * 1. Connects TCP.
     * 2. Sends RequestDataStream.
     * 3. Waits for ResponseRecRule.
     * 4. If successful, creates RecBuffer and starts background thread for data.
     * * @param host Server IP.
     * @param port Server Port.
     * @param bufferName Remote buffer name to request.
     * @return true if buffer exists and stream started, false otherwise.
     */
    bool connect(const std::string& host, uint16_t port, const std::string& bufferName);

    /**
     * @brief Stops reception and disconnects.
     */
    void stop();

    /**
     * @brief Returns the buffer created during the connect phase.
     * Valid immediately after connect() returns true.
     */
    std::shared_ptr<RecBuffer> getBuffer() const;

    /**
     * @brief Check if connected and receiving.
     */
    bool isConnected() const;

    // Optional: Callback when connection is lost/finished
    void setOnFinishedCallback(std::function<void()> cb);

private:
    /**
     * @brief Background loop that handles only incoming DataStreamPayloads.
     */
    void receiveLoop();

private:
    asio::io_context m_ioContext;
    asio::ip::tcp::socket m_socket;

    std::shared_ptr<RecBuffer> m_buffer;
    size_t m_capacity;

    std::thread m_worker;
    std::atomic<bool> m_running;

    std::function<void()> m_onFinished;
};

} // namespace cyc

#endif // CYC_TCPDATARECEIVER_H
