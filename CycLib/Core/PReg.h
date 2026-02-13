// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_PREG_H
#define CYC_PREG_H

#include "CycLib_global.h"
#include <string>
#include <map>
#include <mutex>

namespace cyc {

/**
 * @brief Parameter Registry (Singleton).
 *
 * Maps string names to unique integer IDs to optimize lookups.
 */
class CYCLIB_EXPORT PReg {
public:
    /**
     * @brief Gets the unique ID for a given name.
     * Registers the name if it does not exist.
     * @param name The parameter name.
     * @return The unique ID associated with the name.
     */
    static int getID(const std::string& name);

    /**
     * @brief Gets the name associated with a given ID.
     * @param id The unique ID.
     * @return The name associated with the ID.
     */
    static std::string getName(int id);

private:
    static PReg& instance();
    PReg();
    std::map<std::string, int> nameToId;
    int lastId;
    std::mutex mtx;
};

} // namespace cyc

#endif // CYC_PREG_H
