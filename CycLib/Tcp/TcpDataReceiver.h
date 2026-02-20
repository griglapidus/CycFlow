// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_TCPDATARECEIVER_H
#define CYC_TCPDATARECEIVER_H

#include "Core/CycLib_global.h"
#include "RecordProducer.h"
#include "TcpDefs.h"
#include <asio.hpp>
#include <string>

namespace cyc {

/**
 * @class TcpDataReceiver
 * @brief Receives streaming data over TCP and writes directly to a RecBuffer.
 *
 * Connects to a TcpServer, negotiates the RecRule schema, and continuously
 * requests data batches. Uses zero-copy reads to push data straight into
 * the underlying circular buffer memory.
 */
class CYCLIB_EXPORT TcpDataReceiver : public RecordProducer {
public:
    /**
     * @brief Constructs the receiver.
     * @param bufferCapacity Maximum number of records the target buffer can hold.
     * @param writerBatchSize Number of records to request per network transaction.
     */
    TcpDataReceiver(size_t bufferCapacity = 65536, size_t writerBatchSize = 1000);
    ~TcpDataReceiver() override;

    /**
     * @brief Connects to the server, performs handshake, and starts the receive loop.
     * @param host Server hostname or IP address.
     * @param port Server TCP port.
     * @param bufferName Target buffer name to request from the server.
     * @return True if connection and handshake succeed.
     */
    bool connect(const std::string& host, uint16_t port, const std::string& bufferName);

    /**
     * @brief Safely disconnects and stops the worker thread.
     */
    void stop();

    [[nodiscard]] bool isConnected() const;

protected:
    RecRule defineRule() override;

    /**
     * @brief Custom worker loop for the Request-Response TCP cycle.
     */
    void workerLoop() override;

    void onProduceStart() override {}
    void onProduceStop() override;

    /**
     * @brief Disabled single-record production (uses manual batching in workerLoop).
     */
    bool produceStep(Record& rec) final { return false; }

private:
    asio::io_context m_ioContext;
    asio::ip::tcp::socket m_socket;

    RecRule m_negotiatedRule;
    bool m_connected;
    size_t m_writerBatchSize;
};

} // namespace cyc

#endif // CYC_TCPDATARECEIVER_H
