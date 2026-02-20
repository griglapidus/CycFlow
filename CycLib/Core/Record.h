// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORD_H
#define CYC_RECORD_H

#include "RecRule.h"

namespace cyc {

/**
 * @def DECLARE_RECORD_ACCESSORS
 * @brief Helper macro to generate typed getters and setters for record fields.
 * * @param NAME The capitalized name of the type (e.g., Int32, Double).
 * @param TYPE The actual C++ type (e.g., int32_t, double).
 */
#define DECLARE_RECORD_ACCESSORS(NAME, TYPE) \
TYPE& get##NAME(int id) { return *static_cast<TYPE*>(getVoid(id)); } \
    TYPE* get##NAME##Ptr(int id) { return static_cast<TYPE*>(getVoid(id)); } \
    const TYPE& get##NAME(int id) const { return *static_cast<const TYPE*>(getVoid(id)); } \
    const TYPE* get##NAME##Ptr(int id) const { return static_cast<const TYPE*>(getVoid(id)); } \
    void set##NAME(int id, TYPE val) { *static_cast<TYPE*>(getVoid(id)) = val; }

/**
 * @class Record
 * @brief Represents a single data record instance.
 *
 * This class acts as a lightweight, non-owning view over a raw memory block.
 * It uses a RecRule to safely interpret the raw bytes as typed fields.
 */
class CYCLIB_EXPORT Record {
public:
    /**
     * @brief Constructs a record wrapper.
     * @param r Reference to the schema defining the layout.
     * @param ptr Pointer to the raw data block.
     */
    Record(const RecRule& r, void* ptr);

    /**
     * @brief Checks if the record holds a valid data pointer.
     * @return True if the internal pointer is not null.
     */
    [[nodiscard]] bool isValid() const;

    /**
     * @brief Updates the internal data pointer to point to a new memory block.
     * @param ptr Pointer to the new raw data.
     */
    void setData(void* ptr);

    /**
     * @brief Zeroes out the memory block associated with this record.
     */
    void clear();

    /**
     * @brief Gets the total memory size required for this record.
     * @return Size in bytes.
     */
    [[nodiscard]] size_t getSize() const;

    /**
     * @brief Retrieves the raw data pointer.
     * @return Pointer to the raw memory block.
     */
    [[nodiscard]] void* data();

    /**
     * @brief Retrieves the raw data pointer (const version).
     * @return Const pointer to the raw memory block.
     */
    [[nodiscard]] const void* data() const;

    /**
     * @brief Gets a raw void pointer to a specific field.
     * @param id The unique attribute ID.
     * @return Pointer to the field's location in memory, or nullptr if invalid.
     */
    [[nodiscard]] void* getVoid(int id) const;

    /**
     * @brief Generic getter that converts any fundamental field to a double.
     * Useful for plotting graphs or generic math operations without strict typing.
     * @param id The unique attribute ID.
     * @return The field value cast to a double. Returns 0.0 on failure.
     */
    [[nodiscard]] double getValue(int id) const;

    /**
     * @brief Generic setter that assigns a double value to any fundamental field.
     * @param id The unique attribute ID.
     * @param val The value to set (will be cast to the field's actual type).
     */
    void setValue(int id, double val);

    // --- Typed Accessors ---

    DECLARE_RECORD_ACCESSORS(Bool, bool)
    DECLARE_RECORD_ACCESSORS(Char, char)
    DECLARE_RECORD_ACCESSORS(Int8, int8_t)
    DECLARE_RECORD_ACCESSORS(UInt8, uint8_t)
    DECLARE_RECORD_ACCESSORS(Int16, int16_t)
    DECLARE_RECORD_ACCESSORS(UInt16, uint16_t)
    DECLARE_RECORD_ACCESSORS(Int32, int32_t)
    DECLARE_RECORD_ACCESSORS(UInt32, uint32_t)
    DECLARE_RECORD_ACCESSORS(Int64, int64_t)
    DECLARE_RECORD_ACCESSORS(UInt64, uint64_t)
    DECLARE_RECORD_ACCESSORS(Float, float)
    DECLARE_RECORD_ACCESSORS(Double, double)

    /**
     * @brief Sets a raw pointer value for a pointer-type field.
     * @param id The unique attribute ID.
     * @param val The pointer value to store.
     */
    void setPtr(int id, void* val) { *static_cast<void**>(getVoid(id)) = val; }

    /**
     * @brief Gets a raw pointer value from a pointer-type field.
     * @param id The unique attribute ID.
     * @return The stored pointer value.
     */
    [[nodiscard]] void* getPtr(int id) const { return *static_cast<void**>(getVoid(id)); }

private:
    const RecRule& m_rule;
    uint8_t* m_data;
};

} // namespace cyc

#endif // CYC_RECORD_H
