// TcpServiceClient.h
// SPDX-License-Identifier: MIT

#ifndef CYC_TCPSERVICECLIENT_H
#define CYC_TCPSERVICECLIENT_H

#include "Core/CycLib_global.h"
#include <string>
#include <vector>
#include <cstdint>

namespace cyc {

class CYCLIB_EXPORT TcpServiceClient {
public:
    // Connects to the server, requests the list of buffers, and disconnects.
    // Returns a vector of buffer names.
    static std::vector<std::string> requestBufferList(const std::string& host, uint16_t port);

    // Connects to the server, requests the RecRule for a specific buffer, and disconnects.
    // Returns the RecRule in text format, or an empty string if an error occurs.
    static std::string requestRecRule(const std::string& host, uint16_t port, const std::string& bufferName);
};

} // namespace cyc

#endif // CYC_TCPSERVICECLIENT_H
