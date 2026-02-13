// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "PReg.h"
#include <algorithm>

namespace cyc {

PReg& PReg::instance() { static PReg _inst; return _inst; }
PReg::PReg() : lastId(0) {}

int PReg::getID(const std::string& name) {
    PReg& pReg = instance();
    std::lock_guard<std::mutex> lock(pReg.mtx);
    auto it = pReg.nameToId.find(name);
    if (it != pReg.nameToId.end()) return it->second;
    int newId = ++pReg.lastId;
    pReg.nameToId[name] = newId;
    return newId;
}

std::string PReg::getName(int id) {
    PReg& pReg = instance();
    std::lock_guard<std::mutex> lock(pReg.mtx);
    auto it = std::find_if(pReg.nameToId.begin(), pReg.nameToId.end(),
        [id](const auto& pair) { return pair.second == id; });
    if (it != pReg.nameToId.end()) return it->first;
    return "";
}

} // namespace cyc
