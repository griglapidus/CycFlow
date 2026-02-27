// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "RecRule.h"
#include "PReg.h"
#include <cstring>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace cyc {

// ---------------------------------------------------------------------------
RecRule::RecRule(const std::vector<PAttr>& inputAttrs) {
    init(inputAttrs);
}

// ---------------------------------------------------------------------------
void RecRule::init(const std::vector<PAttr>& inputAttrs) {
    m_attrs.clear();
    m_bitCache.clear();
    size_t tempSize = 0;

    buildHeader();

    for (auto attr : m_headerAttrs) {
        attr.offset = tempSize;
        tempSize += attr.getSize();
        m_attrs.push_back(attr);
    }

    for (auto attr : inputAttrs) {
        attr.offset = tempSize;
        tempSize += attr.getSize();
        m_attrs.push_back(attr);
    }

    // -----------------------------------------------------------------------
    // Build plain-field caches (flat vectors indexed by PReg ID).
    // Only plain field IDs are included — bit IDs go into m_bitCache.
    // -----------------------------------------------------------------------
    int maxFieldId = 0;
    for (const auto& attr : m_attrs) {
        if (attr.id > maxFieldId) maxFieldId = attr.id;
    }

    m_offsetCache.assign(maxFieldId + 1, static_cast<size_t>(-1));
    m_typeCache  .assign(maxFieldId + 1, DataType::dtUndefine);

    for (const auto& attr : m_attrs) {
        if (attr.id > 0 && attr.id <= maxFieldId) {
            m_offsetCache[attr.id] = attr.offset;
            m_typeCache  [attr.id] = attr.type;
        }

        // ── Bit-field cache ─────────────────────────────────────────────
        if (!attr.hasBitFields()) continue;

        for (int bitPos = 0; bitPos < static_cast<int>(attr.bitIds.size()); ++bitPos) {
            const int bid = attr.bitIds[bitPos];
            if (bid == 0) continue;   // reserved/skipped bit

            // Uniqueness check: a named bit must appear in at most one field
            auto [it, inserted] = m_bitCache.emplace(bid, BitRef{attr.id, bitPos});
            if (!inserted) {
                // bid is already registered — determine the conflicting field name
                const std::string existingField = PReg::getName(it->second.fieldId);
                throw std::invalid_argument(
                    std::string("RecRule::init: bit '") + PReg::getName(bid) +
                    "' (id=" + std::to_string(bid) + ") is already registered"
                                                     " in field '" + existingField + "'."
                                      " Bit PReg IDs must be unique across the entire RecRule.");
            }
        }
    }
}

// ---------------------------------------------------------------------------
void RecRule::buildHeader() {
    m_headerAttrs.clear();
    PAttr timestamp("TimeStamp", DataType::dtDouble, 1);
    m_headerAttrs.push_back(timestamp);
}

// ---------------------------------------------------------------------------
size_t RecRule::getRecSize() const {
    return std::accumulate(m_attrs.begin(), m_attrs.end(), size_t(0),
                           [](size_t sum, const PAttr& attr) { return sum + attr.getSize(); });
}

// ---------------------------------------------------------------------------
size_t RecRule::getOffsetByIndex(size_t index) const {
    if (index >= m_attrs.size()) return 0;
    return m_attrs[index].offset;
}

// ---------------------------------------------------------------------------
size_t RecRule::getOffsetById(int id) const {
    if (id > 0 && id < static_cast<int>(m_offsetCache.size())) {
        size_t offset = m_offsetCache[id];
        if (offset != static_cast<size_t>(-1)) return offset;
    }
    return 0;
}

// ---------------------------------------------------------------------------
DataType RecRule::getType(int id) const {
    if (id > 0 && id < static_cast<int>(m_typeCache.size())) {
        DataType t = m_typeCache[id];
        if (t != DataType::dtUndefine) return t;
    }
    return DataType::dtVoid;
}

// ---------------------------------------------------------------------------
BitRef RecRule::getBitRef(int id) const {
    const auto it = m_bitCache.find(id);
    return (it != m_bitCache.end()) ? it->second : BitRef{0, 0};
}

// ---------------------------------------------------------------------------
const std::vector<PAttr>& RecRule::getAttributes() const {
    return m_attrs;
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------
// Format per line:
//   name;Type;count;offset
//   name;Type;count;offset;bitName0,bitName1,4,bitName6,...
// Bit fields use the same notation as PAttr's bitDefs constructor:
//   - named bit  -> its string name
//   - skipped run -> single decimal number (e.g. "4" skips 4 consecutive bits)
// This keeps the text human-readable and round-trips through PAttr(name,type,bitDefs).
// ---------------------------------------------------------------------------
std::string RecRule::toText() const {
    std::stringstream ss;

    for (const auto& attr : m_attrs) {
        ss << attr.name << ';'
           << dataTypeToString(attr.type) << ';'
           << attr.count << ';'
           << attr.offset;

        if (attr.hasBitFields()) {
            const size_t totalBits = attr.getSize() * 8;
            ss << ';';
            bool firstToken = true;
            size_t bit = 0;
            while (bit < totalBits) {
                int bid = (bit < attr.bitIds.size()) ? attr.bitIds[bit] : 0;
                if (bid > 0) {
                    // Named bit
                    if (!firstToken) ss << ',';
                    ss << PReg::getName(bid);
                    firstToken = false;
                    ++bit;
                } else {
                    // Count consecutive reserved/skipped bits and emit as one number
                    size_t skip = 0;
                    while (bit < totalBits &&
                           (bit >= attr.bitIds.size() || attr.bitIds[bit] == 0)) {
                        ++skip;
                        ++bit;
                    }
                    if (!firstToken) ss << ',';
                    ss << skip;
                    firstToken = false;
                }
            }
        }

        ss << '\n';
    }

    return ss.str();
}

// ---------------------------------------------------------------------------
RecRule RecRule::fromText(const std::string& text) {
    // Determine system headers so we can skip them
    RecRule defaultRule;
    defaultRule.buildHeader();
    const auto& systemHeaders = defaultRule.m_headerAttrs;

    std::vector<PAttr> userAttrs;
    std::stringstream  ss(text);
    std::string        line;

    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::stringstream        ls(line);
        std::string              segment;
        std::vector<std::string> parts;

        while (std::getline(ls, segment, ';')) {
            parts.push_back(segment);
        }

        if (parts.size() < 3) continue;

        // Skip system header attributes
        const std::string& attrName = parts[0];
        bool isSystemHeader = false;
        for (const auto& hdr : systemHeaders) {
            if (std::strncmp(hdr.name, attrName.c_str(), sizeof(hdr.name) - 1) == 0) {
                isSystemHeader = true;
                break;
            }
        }
        if (isSystemHeader) continue;

        DataType type  = dataTypeFromString(parts[1].c_str());
        size_t   count = std::stoul(parts[2]);
        // parts[3] = offset (ignored — recalculated in init)

        if (parts.size() >= 5) {
            // Bit-field attribute: reconstruct bitDefs from the 5th column.
            // Format produced by toText: "bitName0,bitName1,4,bitName6,3"
            //   - string token  → named bit (registered in PReg)
            //   - numeric token → skip that many bits
            // Both cases are handled directly by PAttr(name, type, bitDefs).
            std::stringstream        bs(parts[4]);
            std::string              bitCol;
            std::vector<std::string> bitDefs;

            while (std::getline(bs, bitCol, ',')) {
                bitDefs.push_back(bitCol);
            }

            // Only treat as a bit-field attribute if we got at least one token
            if (!bitDefs.empty()) {
                userAttrs.emplace_back(attrName.c_str(), type, bitDefs);
                continue;
            }
        }

        userAttrs.emplace_back(attrName.c_str(), type, count);
    }

    return RecRule(userAttrs);
}

} // namespace cyc
