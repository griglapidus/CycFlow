// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "CycLogger.h"

namespace cyc {

// Set default log level here
std::atomic<LogLevel> g_currentLogLevel{LogLevel::Disabled};

} // namespace cyc
