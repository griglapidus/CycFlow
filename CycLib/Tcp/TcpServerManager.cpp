// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "TcpServerManager.h"
#include "Core/CycLogger.h"

namespace cyc {

TcpServerManager& TcpServerManager::instance() {
    static TcpServerManager mgr;
    return mgr;
}

TcpServerManager::~TcpServerManager() {
    stop();
}

void TcpServerManager::start(uint16_t port) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) {
        LOG_WARN << "TcpServerManager::start() called while already running, ignoring";
        return;
    }

    m_ioContext = std::make_unique<asio::io_context>();
    m_server    = std::make_unique<TcpServer>(*m_ioContext, port);
    m_workGuard = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
        asio::make_work_guard(*m_ioContext)
    );

    m_server->start();
    m_running = true;

    m_thread = std::thread([this]() {
        m_ioContext->run();
    });

    LOG_INFO << "TcpServerManager started on port " << port;
}

void TcpServerManager::stop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running)
        return;

    // Release the work guard so io_context::run() can exit
    m_workGuard.reset();

    // Force-stop any remaining asynchronous operations
    m_ioContext->stop();

    if (m_thread.joinable())
        m_thread.join();

    m_server.reset();
    m_ioContext.reset();
    m_running = false;

    LOG_INFO << "TcpServerManager stopped";
}

bool TcpServerManager::isRunning() const {
    return m_running.load(std::memory_order_acquire);
}

TcpServer* TcpServerManager::server() {
    return m_server.get();
}

asio::io_context* TcpServerManager::ioContext() {
    return m_ioContext.get();
}

} // namespace cyc
