// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Grigorii Lapidus

#ifndef CYC_RECORD_H
#define CYC_RECORD_H

#include "RecRule.h"

namespace cyc {

/**
 * @def DECLARE_RECORD_ACCESSORS
 * @brief Helper macro to generate typed getters and setters for record fields.
 * @param NAME The capitalised name of the type (e.g. Int32, Double).
 * @param TYPE The actual C++ type (e.g. int32_t, double).
 */
#define DECLARE_RECORD_ACCESSORS(NAME, TYPE) \
TYPE& get##NAME(int id, size_t index = 0) { \
        return static_cast<TYPE*>(getVoid(id))[index]; } \
    TYPE* get##NAME##Ptr(int id) { \
        return static_cast<TYPE*>(getVoid(id)); } \
    const TYPE& get##NAME(int id, size_t index = 0) const { \
        return static_cast<const TYPE*>(getVoid(id))[index]; } \
    const TYPE* get##NAME##Ptr(int id) const { \
        return static_cast<const TYPE*>(getVoid(id)); } \
    void set##NAME(int id, TYPE val, size_t index = 0) { \
        static_cast<TYPE*>(getVoid(id))[index] = val; }

/**
 * @class Record
 * @brief Represents a single data record instance.
 *
 * Lightweight, non-owning view over a raw memory block.
 * Uses a RecRule to safely interpret raw bytes as typed fields and named bits.
 *
 * **Bit-field access**
 *
 * Named bits are accessed by the PReg ID registered through PAttr's `bitDefs`
 * constructor.  Internally the class resolves the bit to its containing byte
 * and bit index using a O(1) cache inside RecRule.
 *
 * @code
 * // Schema setup
 * PAttr flags("StatusReg", DataType::dtUInt8, {"txReady", "rxReady", "4", "errFlag"});
 * RecRule rule({ flags });
 *
 * // Allocation and access
 * std::vector<uint8_t> buf(rule.getRecSize(), 0);
 * Record rec(rule, buf.data());
 *
 * int idTx  = PReg::getID("txReady");
 * int idErr = PReg::getID("errFlag");
 *
 * rec.setBit(idTx,  true);
 * rec.setBit(idErr, true);
 * bool tx  = rec.getBit(idTx);   // true
 * bool err = rec.getBit(idErr);  // true
 * @endcode
 */
class CYCLIB_EXPORT Record {
public:
    /**
     * @brief Constructs a record wrapper.
     * @param r   Schema defining the memory layout.
     * @param ptr Pointer to the raw data block (not owned by Record).
     */
    Record(const RecRule& r, void* ptr);

    /**
     * @brief Checks if the record holds a valid (non-null) data pointer.
     */
    [[nodiscard]] bool isValid() const;

    /**
     * @brief Updates the internal data pointer to a new memory block.
     * @param ptr Pointer to the new raw data.
     */
    void setData(void* ptr);

    /**
     * @brief Zeroes out the entire memory block associated with this record.
     */
    void clear();

    /**
     * @brief Gets the total memory size required for this record.
     * @return Size in bytes.
     */
    [[nodiscard]] size_t getSize() const;

    /** @brief Retrieves the raw mutable data pointer. */
    [[nodiscard]] void* data();

    /** @brief Retrieves the raw const data pointer. */
    [[nodiscard]] const void* data() const;

    /**
     * @brief Gets a raw void pointer to the start of a named field.
     * @param id PReg ID of the attribute.
     * @return Pointer to the field, or nullptr if invalid.
     */
    [[nodiscard]] void* getVoid(int id) const;

    // -----------------------------------------------------------------------
    // Generic double-based accessors (work on plain fields)
    // -----------------------------------------------------------------------

    /**
     * @brief Reads any fundamental field as a double.
     * @param id    PReg ID of the attribute.
     * @param index Array index (0 for scalars).
     * @return Field value cast to double; 0.0 on failure.
     */
    [[nodiscard]] double getValue(int id, size_t index = 0) const;

    /**
     * @brief Writes a double value to any fundamental field (cast to target type).
     * @param id    PReg ID of the attribute.
     * @param val   Value to assign.
     * @param index Array index (0 for scalars).
     */
    void setValue(int id, double val, size_t index = 0);

    // -----------------------------------------------------------------------
    // Bit-field accessors
    // -----------------------------------------------------------------------

    /**
     * @brief Reads a single named bit.
     *
     * The @p id must be a PReg ID registered via PAttr's `bitDefs` constructor,
     * not the ID of the containing integer field.
     *
     * @param id PReg ID of the named bit.
     * @return The bit value (true / false), or false if @p id is unknown.
     */
    [[nodiscard]] bool getBit(int id) const;

    /**
     * @brief Writes a single named bit.
     *
     * @param id  PReg ID of the named bit.
     * @param val Value to write.
     */
    void setBit(int id, bool val);

    /**
     * @brief Reads a named bit and returns it as a double (0.0 or 1.0).
     *
     * Convenience wrapper for use in generic pipelines that work entirely
     * with doubles (e.g. logging, scripting).
     *
     * @param id PReg ID of the named bit.
     * @return 1.0 if the bit is set, 0.0 otherwise.
     */
    [[nodiscard]] double getBitValue(int id) const;

    /**
     * @brief Writes a named bit from a double value.
     *
     * Any non-zero value sets the bit; zero clears it.
     *
     * @param id  PReg ID of the named bit.
     * @param val Value to write.
     */
    void setBitValue(int id, double val);

    // -----------------------------------------------------------------------
    // Typed accessors (generated via macro)
    // -----------------------------------------------------------------------

    DECLARE_RECORD_ACCESSORS(Bool,   bool)
    DECLARE_RECORD_ACCESSORS(Char,   char)
    DECLARE_RECORD_ACCESSORS(Int8,   int8_t)
    DECLARE_RECORD_ACCESSORS(UInt8,  uint8_t)
    DECLARE_RECORD_ACCESSORS(Int16,  int16_t)
    DECLARE_RECORD_ACCESSORS(UInt16, uint16_t)
    DECLARE_RECORD_ACCESSORS(Int32,  int32_t)
    DECLARE_RECORD_ACCESSORS(UInt32, uint32_t)
    DECLARE_RECORD_ACCESSORS(Int64,  int64_t)
    DECLARE_RECORD_ACCESSORS(UInt64, uint64_t)
    DECLARE_RECORD_ACCESSORS(Float,  float)
    DECLARE_RECORD_ACCESSORS(Double, double)

    /** @brief Stores a raw pointer value in a pointer-type field. */
    void setPtr(int id, void* val, size_t index = 0) {
        static_cast<void**>(getVoid(id))[index] = val;
    }

    /** @brief Retrieves a raw pointer value from a pointer-type field. */
    [[nodiscard]] void* getPtr(int id, size_t index = 0) const {
        return static_cast<void**>(getVoid(id))[index];
    }

private:
    const RecRule& m_rule;
    uint8_t*       m_data;
};

} // namespace cyc

#endif // CYC_RECORD_H
