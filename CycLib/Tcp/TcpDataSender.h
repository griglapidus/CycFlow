// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_TCPDATASENDER_H
#define CYC_TCPDATASENDER_H

#include "Core/CycLib_global.h"
#include "RecordConsumer.h"
#include <asio.hpp>

namespace cyc {

/**
 * @class TcpDataSender
 * @brief Serves data requests from a connected client.
 *
 * Inherits RecordConsumer to manage the underlying RecordReader lifecycle,
 * but overrides the main loop to act as a TCP Request-Response server.
 */
class CYCLIB_EXPORT TcpDataSender : public RecordConsumer {
public:
    TcpDataSender(std::shared_ptr<RecBuffer> buffer, asio::ip::tcp::socket socket);
    ~TcpDataSender() override;

protected:
    /**
     * @brief Custom loop to handle the request-response cycle.
     * Waits for RequestDataBatch -> Reads from RecBuffer -> Sends Response.
     */
    void workerLoop() override;

    /**
     * @brief Disabled single-record consumption (uses manual batching).
     */
    void consumeRecord(const Record& rec) final {}

private:
    asio::ip::tcp::socket m_socket;
};

} // namespace cyc

#endif // CYC_TCPDATASENDER_H
