// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "PReg.h"
#include <mutex>

namespace cyc {

PReg& PReg::instance() {
    static PReg _inst;
    return _inst;
}

PReg::PReg() {}

int PReg::getID(const std::string& name) {
    PReg& pReg = instance();

    // Fast path: shared lock for reading
    {
        std::shared_lock<std::shared_mutex> lock(pReg.m_mtx);
        auto it = pReg.m_nameToId.find(name);
        if (it != pReg.m_nameToId.end()) {
            return it->second;
        }
    }

    // Slow path: exclusive lock for writing
    std::unique_lock<std::shared_mutex> lock(pReg.m_mtx);
    // Double-checked locking to prevent race conditions during insertion
    auto it = pReg.m_nameToId.find(name);
    if (it != pReg.m_nameToId.end()) {
        return it->second;
    }

    pReg.m_idToName.push_back(name);
    int newId = static_cast<int>(pReg.m_idToName.size()); // 1-based indexing
    pReg.m_nameToId[name] = newId;

    return newId;
}

std::string PReg::getName(int id) {
    PReg& pReg = instance();
    // Only shared lock is needed for array lookup
    std::shared_lock<std::shared_mutex> lock(pReg.m_mtx);

    if (id > 0 && static_cast<size_t>(id) <= pReg.m_idToName.size()) {
        return pReg.m_idToName[id - 1]; // 1-based to 0-based index
    }
    return "";
}

} // namespace cyc
