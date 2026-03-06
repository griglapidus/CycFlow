// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_IRECBUFFERCLIENT_H
#define CYC_IRECBUFFERCLIENT_H

#include <cstdint>
#include "CycLib_global.h"

namespace cyc {

/**
 * @class IRecBufferClient
 * @brief Interface for clients consuming data from a RecBuffer.
 *
 * Defines the contract between the circular buffer and its consumers (e.g., RecordReader).
 */
class CYCLIB_EXPORT IRecBufferClient {
public:
    virtual ~IRecBufferClient() = default;

    /**
     * @brief Called by the RecBuffer when new data has been written.
     * * @warning This method is invoked directly from the Writer's thread.
     * Implementations should be thread-safe and execute as quickly as possible
     * to avoid blocking the data producer.
     */
    virtual void notifyDataAvailable() = 0;

    /**
     * @brief Returns the current read cursor position of the client.
     * * This value is used by the buffer to calculate the safe write zone,
     * ensuring that unread data is not overwritten if the writer operates
     * in a blocking mode.
     * * @return The absolute index of the client's read cursor. If the client is
     * passive (e.g., a real-time UI graph) and should not block the writer,
     * it must return UINT64_MAX.
     */
    [[nodiscard]] virtual uint64_t getCursor() const = 0;
};

} // namespace cyc

#endif // CYC_IRECBUFFERCLIENT_H
