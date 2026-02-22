#ifndef CHARTDEFS_H
#define CHARTDEFS_H

#include <QVector>
#include <QString>
#include <QColor>
#include <variant>
#include <cstdint>
#include <cfloat>

// ─── SampleBuffer ─────────────────────────────────────────────────────────────

using SampleBuffer = std::variant<
    QVector<int8_t>,    // 0
    QVector<uint8_t>,   // 1
    QVector<int16_t>,   // 2
    QVector<uint16_t>,  // 3
    QVector<int32_t>,   // 4
    QVector<uint32_t>,  // 5
    QVector<int64_t>,   // 6
    QVector<uint64_t>,  // 7
    QVector<float>,     // 8
    QVector<double>     // 9
    >;

enum class SampleType : int {
    Int8    = 0, UInt8   = 1,
    Int16   = 2, UInt16  = 3,
    Int32   = 4, UInt32  = 5,
    Int64   = 6, UInt64  = 7,
    Float32 = 8, Float64 = 9
};

// ─── BoundsValue — хранит min/max в нативном типе ────────────────────────────
//
//  double  : float32, float64, int8..int32, uint8..uint32  (53 бит мантиссы достаточно)
//  int64_t : int64   (полные 63 бит значимых)
//  uint64_t: uint64  (полные 64 бит значимых)

using BoundsValue = std::variant<double, int64_t, uint64_t>;

// Начальные значения bounds для каждого SampleType
inline std::pair<BoundsValue,BoundsValue> makeBounds(SampleType t)
{
    switch (t) {
    case SampleType::Int64:
        return { int64_t(INT64_MAX), int64_t(INT64_MIN) };
    case SampleType::UInt64:
        return { uint64_t(UINT64_MAX), uint64_t(0) };
    default:
        return { double(DBL_MAX), double(-DBL_MAX) };
    }
}

// Приближённое значение для пиксельной арифметики (только для UI)
inline double boundsToDouble(const BoundsValue &b)
{
    return std::visit([](const auto &v) { return static_cast<double>(v); }, b);
}

// ─── SampleBuffer helpers ─────────────────────────────────────────────────────

inline int sampleCount(const SampleBuffer &buf)
{
    return std::visit([](const auto &v) { return v.size(); }, buf);
}

inline bool sampleIsEmpty(const SampleBuffer &buf)
{
    return std::visit([](const auto &v) { return v.isEmpty(); }, buf);
}

// Универсальный sampleAt → double.
// Для int64/uint64 теряет точность при |v| > 2^53 — используйте
// sampleAtI64 / sampleAtU64 когда нужна полная точность.
inline double sampleAt(const SampleBuffer &buf, int i)
{
    return std::visit([i](const auto &v) -> double {
        return static_cast<double>(v[i]);
    }, buf);
}

// Точный доступ для знаковых 64-битных данных (int8..int64).
// Для float/double — округляет до int64_t (используйте sampleAt).
inline int64_t sampleAtI64(const SampleBuffer &buf, int i)
{
    return std::visit([i](const auto &v) -> int64_t {
        using T = std::decay_t<decltype(v[0])>;
        if constexpr (std::is_unsigned_v<T>)
            return static_cast<int64_t>(v[i]);   // uint64→int64: UB если > INT64_MAX
        else
            return static_cast<int64_t>(v[i]);
    }, buf);
}

// Точный доступ для беззнаковых 64-битных данных (uint8..uint64).
inline uint64_t sampleAtU64(const SampleBuffer &buf, int i)
{
    return std::visit([i](const auto &v) -> uint64_t {
        return static_cast<uint64_t>(v[i]);
    }, buf);
}

// Вычисляет (sample[i] - lo) / (hi - lo) ∈ [0..1] с нативной точностью.
//
// Для int64/uint64: вычитание делается в нативной арифметике (избегаем
// потери значимых разрядов при конвертации в double до вычитания).
// Для всех остальных типов — обычная double арифметика.
inline double sampleRatio(const SampleBuffer &buf, int i,
                          const BoundsValue &lo, const BoundsValue &hi)
{
    const std::size_t idx = buf.index();

    if (idx == 6) {  // int64
        const int64_t v    = sampleAtI64(buf, i);
        const int64_t loV  = std::get<int64_t>(lo);
        const int64_t hiV  = std::get<int64_t>(hi);
        if (hiV == loV) return 0.5;
        // Вычитаем в int64 → разность помещается в int64 (lo ≤ v ≤ hi)
        return static_cast<double>(v - loV) / static_cast<double>(hiV - loV);
    }

    if (idx == 7) {  // uint64
        const uint64_t v   = sampleAtU64(buf, i);
        const uint64_t loV = std::get<uint64_t>(lo);
        const uint64_t hiV = std::get<uint64_t>(hi);
        if (hiV == loV) return 0.5;
        // Вычитаем в uint64 — безопасно (v ≥ loV гарантировано bounds)
        return static_cast<double>(v - loV) / static_cast<double>(hiV - loV);
    }

    // Все остальные типы: double достаточно точен
    const double v    = sampleAt(buf, i);
    const double loV  = boundsToDouble(lo);
    const double hiV  = boundsToDouble(hi);
    if (qFuzzyCompare(loV, hiV)) return 0.5;
    return (v - loV) / (hiV - loV);
}

// Имя типа для UI
inline const char *sampleTypeName(const SampleBuffer &buf)
{
    static const char *names[] = {
        "int8","uint8","int16","uint16","int32","uint32",
        "int64","uint64","float32","float64"
    };
    const std::size_t idx = buf.index();
    return (idx < std::size(names)) ? names[idx] : "?";
}

inline SampleBuffer makeSampleBuffer(SampleType t)
{
    switch (t) {
    case SampleType::Int8:    return QVector<int8_t>{};
    case SampleType::UInt8:   return QVector<uint8_t>{};
    case SampleType::Int16:   return QVector<int16_t>{};
    case SampleType::UInt16:  return QVector<uint16_t>{};
    case SampleType::Int32:   return QVector<int32_t>{};
    case SampleType::UInt32:  return QVector<uint32_t>{};
    case SampleType::Int64:   return QVector<int64_t>{};
    case SampleType::UInt64:  return QVector<uint64_t>{};
    case SampleType::Float32: return QVector<float>{};
    case SampleType::Float64: return QVector<double>{};
    }
    return QVector<float>{};
}

// ─── ChartSeries ──────────────────────────────────────────────────────────────

struct ChartSeries {
    QString      name;
    QColor       color    = Qt::green;
    SampleBuffer data     = QVector<float>{};
    BoundsValue  minVal   = double( DBL_MAX);   // тип определяется в addSeries()
    BoundsValue  maxVal   = double(-DBL_MAX);
};

#endif // CHARTDEFS_H
