// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#include "RecRule.h"
#include <cstring>
#include <numeric>
#include <algorithm>
#include <iterator>
#include <sstream>

namespace cyc {

cyc::RecRule::RecRule(const std::vector<PAttr> &inputAttrs) {
    init(inputAttrs);
}

void RecRule::init(const std::vector<PAttr> &inputAttrs)
{
    m_attrs.clear();
    size_t tempSize = 0;
    buildHeader();

    for(auto attr : m_headerAttrs) {
        attr.offset = tempSize;
        tempSize = attr.offset + attr.getSize();
        m_attrs.push_back(attr);
    }

    for(auto attr : inputAttrs) {
        attr.offset = tempSize;
        tempSize = attr.offset + attr.getSize();
        m_attrs.push_back(attr);
    }
}

void RecRule::buildHeader()
{
    m_headerAttrs.clear();
    PAttr timestamp("TimeStamp",DataType::dtDouble,1);
    m_headerAttrs.push_back(timestamp);
}

size_t RecRule::getRecSize() const {
    return std::accumulate(m_attrs.begin(), m_attrs.end(), size_t(0),
        [](size_t sum, const PAttr& attr) { return sum + attr.getSize(); });
}
size_t RecRule::getOffsetByIndex(size_t index) const {
    if(index >= m_attrs.size()) return 0;
    return m_attrs[index].offset;
}
size_t RecRule::getOffsetById(int id) const {
    auto it = std::find_if(m_attrs.begin(), m_attrs.end(),
        [id](const PAttr& attr) { return attr.id == id; });
    if (it != m_attrs.end()) return getOffsetByIndex(std::distance(m_attrs.begin(), it));
    return 0;
}
DataType RecRule::getType(int id) const {
    auto it = std::find_if(m_attrs.begin(), m_attrs.end(),
        [id](const PAttr& attr) { return attr.id == id; });
    if (it != m_attrs.end()) return it->type;
    return DataType::dtVoid; 
}
const std::vector<PAttr>& RecRule::getAttributes() const { return m_attrs; }

std::string RecRule::toText() const {
    std::stringstream ss;
    for (const auto& attr : m_attrs) {
        ss << attr.name << ";"
           << dataTypeToString(attr.type) << ";"
           << attr.count << ";"
           << attr.offset << "\n";
    }
    return ss.str();
}

RecRule RecRule::fromText(const std::string& text) {
    // 1. Create a temporary default rule to identify current system headers.
    RecRule defaultRule;
    defaultRule.buildHeader();
    // defaultRule.init() is called in constructor -> buildHeader() is called.
    const auto& systemHeaders = defaultRule.m_headerAttrs;

    std::vector<PAttr> userAttrs;
    std::stringstream ss(text);
    std::string line;

    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back(); // Handle Windows line endings

        std::stringstream ls(line);
        std::string segment;
        std::vector<std::string> parts;

        while(std::getline(ls, segment, ';')) {
            parts.push_back(segment);
        }

        // Format: Name;Type;Count;Offset
        if (parts.size() >= 3) {
            std::string name = parts[0];

            // 2. Check if this attribute is one of the system headers
            bool isSystemHeader = false;
            for (const auto& header : systemHeaders) {
                // Compare name string with header name
                if (strncmp(header.name, name.c_str(), 25) == 0) {
                    isSystemHeader = true;
                    break;
                }
            }

            // 3. Skip system headers to avoid duplication in RecRule::init()
            if (isSystemHeader) {
                continue;
            }

            DataType type = dataTypeFromString(parts[1].c_str());
            size_t count = std::stoul(parts[2]);

            // Offset is ignored as it is recalculated
            userAttrs.emplace_back(name.c_str(), type, count);
        }
    }

    // Create RecRule. This calls init(), which prepends systemHeaders and recalculates offsets.
    return RecRule(userAttrs);
}
} // namespace cyc
