// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_TCPSERVERMANAGER_H
#define CYC_TCPSERVERMANAGER_H

#include "Core/CycLib_global.h"
#include "TcpServer.h"
#include <asio.hpp>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

namespace cyc {
CYCLIB_SUPPRESS_C4251

/**
 * @class TcpServerManager
 * @brief Singleton that owns an asio::io_context and a TcpServer.
 *
 * Provides global access to a single TcpServer instance from anywhere in
 * the application. When start() is called, an io_context and TcpServer are
 * created and the event loop runs in a dedicated background thread.
 * When not started, server() and ioContext() return nullptr.
 *
 * Supports stop() + start() restart cycles (e.g. to change port).
 *
 * Usage:
 * @code
 *   auto& mgr = cyc::TcpServerManager::instance();
 *   mgr.start();          // default port 5000
 *   mgr.server()->registerBuffer("buf", buffer, 500);
 *   // ... later ...
 *   mgr.stop();
 *   mgr.start(6000);      // restart on a different port
 * @endcode
 */
class CYCLIB_EXPORT TcpServerManager {
public:
    /**
     * @brief Returns the singleton instance.
     */
    static TcpServerManager& instance();

    /**
     * @brief Start the server on the given port.
     *
     * Creates io_context, TcpServer, and launches a background thread
     * running the ASIO event loop. Does nothing if already running.
     *
     * @param port TCP port to listen on (default: 5000).
     */
    void start(uint16_t port = 5000);

    /**
     * @brief Stop the server and join the background thread.
     *
     * Destroys the TcpServer and io_context. After stop() returns,
     * start() can be called again (possibly with a different port).
     * Does nothing if not running.
     */
    void stop();

    /**
     * @brief Check whether the server is currently running.
     */
    bool isRunning() const;

    /**
     * @brief Access the managed TcpServer.
     * @return Pointer to the server, or nullptr if not started.
     */
    TcpServer* server();

    /**
     * @brief Access the managed io_context.
     * @return Pointer to the io_context, or nullptr if not started.
     */
    asio::io_context* ioContext();

private:
    TcpServerManager() = default;
    ~TcpServerManager();

    TcpServerManager(const TcpServerManager&) = delete;
    TcpServerManager& operator=(const TcpServerManager&) = delete;

private:
    std::unique_ptr<asio::io_context> m_ioContext;
    std::unique_ptr<TcpServer>        m_server;

    /// Work guard keeps io_context::run() alive when there are no pending handlers.
    std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> m_workGuard;

    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    mutable std::mutex m_mutex;
};

CYCLIB_RESTORE_C4251
} // namespace cyc

#endif // CYC_TCPSERVERMANAGER_H
