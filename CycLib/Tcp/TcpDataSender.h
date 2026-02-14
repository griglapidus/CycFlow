// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_TCPDATASENDER_H
#define CYC_TCPDATASENDER_H

#include "Core/CycLib_global.h"
#include "RecordConsumer.h" // Здесь определен BatchRecordConsumer
#include "TcpDefs.h"
#include <asio.hpp>
#include <vector>

namespace cyc {

/**
 * @brief Reads records from a RecBuffer and streams them over a TCP socket.
 * Uses BatchRecordConsumer to send raw memory blocks directly to the socket,
 * minimizing memory copying.
 */
class CYCLIB_EXPORT TcpDataSender : public BatchRecordConsumer {
public:
    /**
     * @brief Constructs the sender.
     * @param buffer Source RecBuffer to read from.
     * @param socket Connected TCP socket (transferred ownership).
     * @param batchSize Number of records to read and send in one chunk.
     */
    TcpDataSender(std::shared_ptr<RecBuffer> buffer, asio::ip::tcp::socket socket, size_t batchSize = 100);

    virtual ~TcpDataSender();

protected:
    /**
     * @brief Called by the worker thread for each batch of records.
     * Constructs a TCP packet header and sends it along with the raw batch data.
     */
    void consumeBatch(const RecordReader::RecordBatch& batch) override;

    // consumeRecord is not used in BatchRecordConsumer (it is final empty)

private:
    asio::ip::tcp::socket m_socket;
};

} // namespace cyc

#endif // CYC_TCPDATASENDER_H
