// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECRULE_H
#define CYC_RECRULE_H

#include "PAttr.h"
#include <vector>

namespace cyc {

/**
 * @brief Defines the structure (schema) of a record.
 *
 * Manages the list of attributes and calculates their offsets.
 */
class CYCLIB_EXPORT RecRule {
public:
    RecRule(){}

    /**
     * @brief Constructs a rule from a list of attributes.
     * @param inputAttrs Vector of attributes.
     */
    RecRule(const std::vector<PAttr>& inputAttrs);

    /**
     * @brief Initializes the rule with attributes and calculates offsets.
     * @param inputAttrs Vector of attributes.
     */
    void init(const std::vector<PAttr>& inputAttrs);

    /**
     * @brief Builds internal header information for the rule.
     */
    void buildHeader();

    /**
     * @brief Gets the total size of a record defined by this rule.
     * @return Total size in bytes.
     */
    size_t getRecSize() const;

    /**
     * @brief Gets the memory offset of an attribute by its index in the list.
     * @param index Index in the attribute vector.
     * @return Offset in bytes.
     */
    size_t getOffsetByIndex(size_t index) const;

    /**
     * @brief Gets the memory offset of an attribute by its unique ID.
     * @param id Unique ID of the attribute.
     * @return Offset in bytes.
     */
    size_t getOffsetById(int id) const;

    /**
     * @brief Gets the data type of an attribute by its ID.
     * @param id Unique ID of the attribute.
     * @return Data type.
     */
    DataType getType(int id) const;

    /**
     * @brief Retrieves the list of attributes.
     * @return Constant reference to the attribute vector.
     */
    const std::vector<PAttr>& getAttributes() const;

private:
    std::vector<PAttr> m_headerAttrs;
    std::vector<PAttr> m_attrs;
};

} // namespace cyc

#endif // CYC_RECRULE_H
