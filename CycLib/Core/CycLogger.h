// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_LOGGER_H
#define CYC_LOGGER_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <thread>
#include <atomic>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <processthreadsapi.h>
#elif defined(__APPLE__)
#include <unistd.h>
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

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

inline void setLogLevel(LogLevel level) {
    g_currentLogLevel.store(level, std::memory_order_release);
}

/**
 * @brief Manages per-process log file creation and access.
 *
 * Resolves the current executable name and creates a log file
 * in the form: <executable_name>_<pid>.log
 * Thread-safe singleton accessed via instance().
 */
class LogFileManager {
public:
    static LogFileManager& instance() {
        static LogFileManager mgr;
        return mgr;
    }

    std::ofstream& stream() { return m_file; }
    bool isOpen() const { return m_file.is_open(); }
    const std::string& filePath() const { return m_path; }

    /**
     * @brief Override the default log directory (must be called before first log message).
     */
    static void setLogDirectory(const std::string& dir) {
        s_logDir() = dir;
    }

private:
    LogFileManager() {
        std::string baseName = resolveProcessName();

        long pid = 0;
#ifdef _WIN32
        pid = static_cast<long>(GetCurrentProcessId());
#else
        pid = static_cast<long>(getpid());
#endif

        std::string dir = s_logDir().empty() ? "." : s_logDir();
        m_path = dir + "/" + baseName + "_" + std::to_string(pid) + ".log";

        m_file.open(m_path, std::ios::out | std::ios::app);

        // Always print to stderr so the user knows where the log file is
        if (m_file.is_open()) {
            std::cerr << "[CycLogger] Log file opened: " << m_path << std::endl;
            m_file << "=== Log started for process: " << baseName
                   << " (PID " << pid << ") ===" << std::endl;
        } else {
            std::cerr << "[CycLogger] ERROR: Failed to open log file: " << m_path << std::endl;
        }
    }

    ~LogFileManager() {
        if (m_file.is_open()) {
            m_file << "=== Log ended ===" << std::endl;
            m_file.close();
        }
    }

    static std::string resolveProcessName() {
        std::string name = "unknown";

#ifdef _WIN32
        char buf[MAX_PATH];
        DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (len > 0) {
            name = buf;
            auto pos = name.find_last_of("\\/");
            if (pos != std::string::npos) name = name.substr(pos + 1);
            auto dot = name.rfind('.');
            if (dot != std::string::npos) name = name.substr(0, dot);
        }
#elif defined(__APPLE__)
        char buf[1024];
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) == 0) {
            name = buf;
            auto pos = name.find_last_of('/');
            if (pos != std::string::npos) name = name.substr(pos + 1);
        }
#else
        // Linux: read /proc/self/exe symlink
        char buf[1024];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            name = buf;
            auto pos = name.find_last_of('/');
            if (pos != std::string::npos) name = name.substr(pos + 1);
        }
#endif
        return name;
    }

    static std::string& s_logDir() {
        static std::string dir;
        return dir;
    }

    std::string m_path;
    std::ofstream m_file;

    LogFileManager(const LogFileManager&) = delete;
    LogFileManager& operator=(const LogFileManager&) = delete;
};

/**
 * @brief Thread-safe logging helper.
 *
 * Collects data into a stream and flushes it atomically
 * to both stdout/stderr and the per-process log file in the destructor.
 */
class LogMessage {
public:
    LogMessage(LogLevel level, const char* file, int line)
        : m_level(level) {

        // Timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) % 1000;

        // Thread-safe localtime resolution
        std::tm tm_snapshot;
#ifdef _WIN32
        localtime_s(&tm_snapshot, &time);
#else
        localtime_r(&time, &tm_snapshot);
#endif

        m_buffer << "[" << std::put_time(&tm_snapshot, "%T")
                 << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";

        // Thread ID
        m_buffer << "[TID:" << std::this_thread::get_id() << "] ";

        // Level tag
        switch (level) {
        case LogLevel::Error:    m_buffer << "[ERR] "; break;
        case LogLevel::Warning:  m_buffer << "[WRN] "; break;
        case LogLevel::Info:     m_buffer << "[INF] "; break;
        case LogLevel::Debug:    m_buffer << "[DBG] "; break;
        case LogLevel::Trace:    m_buffer << "[TRC] "; break;
        case LogLevel::Disabled: break;
        }

        // Source location (debug/trace only to reduce noise)
        if (level >= LogLevel::Debug) {
            const char* shortFile = file;
            for (const char* p = file; *p; ++p) {
                if (*p == '/' || *p == '\\') shortFile = p + 1;
            }
            m_buffer << "[" << shortFile << ":" << line << "] ";
        }
    }

    ~LogMessage() {
        m_buffer << "\n";
        std::string msg = m_buffer.str();

        std::lock_guard<std::mutex> lock(getMutex());

        // Write to console
        if (m_level == LogLevel::Error) {
            std::cerr << msg;
        } else {
            std::cout << msg;
        }

        // Write to per-process log file
        auto& mgr = LogFileManager::instance();
        if (mgr.isOpen()) {
            mgr.stream() << msg;
            mgr.stream().flush();
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
