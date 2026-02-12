// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_LOGGER_H
#define CYC_LOGGER_H

#include <iostream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <thread>
#include <atomic>

namespace cyc {

enum class LogLevel {
    Disabled = 0,
    Error,
    Warning,
    Info,
    Debug,
    Trace
};

// Global log level control
extern std::atomic<LogLevel> g_currentLogLevel;

/**
 * @brief Thread-safe logging helper.
 * * Collects data into a stream and flushes it atomically to stdout/stderr in the destructor.
 */
class LogMessage {
public:
    LogMessage(LogLevel level, const char* file, int line)
        : m_level(level) {

        // Timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        m_buffer << "[" << std::put_time(std::localtime(&time), "%T")
                 << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";

        // Thread ID
        m_buffer << "[TID:" << std::this_thread::get_id() << "] ";

        // Level
        switch (level) {
        case LogLevel::Error:   m_buffer << "[ERR] "; break;
        case LogLevel::Warning: m_buffer << "[WRN] "; break;
        case LogLevel::Info:    m_buffer << "[INF] "; break;
        case LogLevel::Debug:   m_buffer << "[DBG] "; break;
        case LogLevel::Trace:   m_buffer << "[TRC] "; break;
        case LogLevel::Disabled:m_buffer << ""; break;
            break;
        }
    }

    ~LogMessage() {
        m_buffer << "\n";
        // Atomic output to prevent mixed lines from different threads
        std::lock_guard<std::mutex> lock(getMutex());
        if (m_level == LogLevel::Error) {
            std::cerr << m_buffer.str();
        } else {
            std::cout << m_buffer.str();
        }
    }

    template <typename T>
    LogMessage& operator<<(const T& value) {
        m_buffer << value;
        return *this;
    }

private:
    static std::mutex& getMutex() {
        static std::mutex mtx;
        return mtx;
    }

    LogLevel m_level;
    std::ostringstream m_buffer;
};

} // namespace cyc

// Macros for easy usage
// They check the level BEFORE creating the LogMessage object for performance
#define LOG_ERR \
if (cyc::g_currentLogLevel >= cyc::LogLevel::Error) \
    cyc::LogMessage(cyc::LogLevel::Error, __FILE__, __LINE__)

#define LOG_WARN \
    if (cyc::g_currentLogLevel >= cyc::LogLevel::Warning) \
    cyc::LogMessage(cyc::LogLevel::Warning, __FILE__, __LINE__)

#define LOG_INFO \
    if (cyc::g_currentLogLevel >= cyc::LogLevel::Info) \
    cyc::LogMessage(cyc::LogLevel::Info, __FILE__, __LINE__)

#define LOG_DBG \
    if (cyc::g_currentLogLevel >= cyc::LogLevel::Debug) \
    cyc::LogMessage(cyc::LogLevel::Debug, __FILE__, __LINE__)

#define LOG_TRACE \
    if (cyc::g_currentLogLevel >= cyc::LogLevel::Trace) \
    cyc::LogMessage(cyc::LogLevel::Trace, __FILE__, __LINE__)

#endif // CYC_LOGGER_H
