// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_PREG_H
#define CYC_PREG_H

#include "CycLib_global.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <shared_mutex>

namespace cyc {

/**
 * @class PReg
 * @brief Parameter Registry (Singleton).
 *
 * Thread-safe registry that maps string names to unique integer IDs to optimize lookups.
 * Optimized for heavy read operations with O(1) complexity for both ID and Name lookups.
 */
class CYCLIB_EXPORT PReg {
public:
    /**
     * @brief Gets the unique ID for a given name. Registers it if it does not exist.
     * @param name The parameter name.
     * @return The unique ID associated with the name.
     */
    static int getID(const std::string& name);

    /**
     * @brief Gets the name associated with a given ID.
     * @param id The unique ID.
     * @return The name associated with the ID. Returns empty string if not found.
     */
    static std::string getName(int id);

private:
    static PReg& instance();
    PReg();

    std::unordered_map<std::string, int> m_nameToId;
    std::vector<std::string> m_idToName; // Flat array for O(1) reverse lookup
    mutable std::shared_mutex m_mtx;
};

} // namespace cyc

#endif // CYC_PREG_H
