// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_TCPDATASENDER_H
#define CYC_TCPDATASENDER_H

#include "Core/CycLib_global.h"
#include "RecordConsumer.h"
#include "TcpDefs.h"
#include <asio.hpp>
#include <vector>

namespace cyc {

/**
 * @brief Serves data requests from a connected client.
 * Inherits RecordConsumer to manage the Reader lifecycle, but overrides
 * the loop to act as a Server (Request-Response).
 */
class CYCLIB_EXPORT TcpDataSender : public RecordConsumer {
public:
    TcpDataSender(std::shared_ptr<RecBuffer> buffer, asio::ip::tcp::socket socket);
    virtual ~TcpDataSender();

protected:
    /**
     * @brief Custom loop to handle request-response cycle.
     * Waits for RequestDataBatch -> Reads from Reader -> Sends Response.
     */
    void workerLoop() override;

    void consumeRecord(const Record& rec) override {}

private:
    asio::ip::tcp::socket m_socket;
};

} // namespace cyc

#endif // CYC_TCPDATASENDER_H
