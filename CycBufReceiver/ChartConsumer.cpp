#include "ChartConsumer.h"
#include "Core/PAttr.h"
#include "Core/PReg.h"
#include <QVariant>

ChartConsumer::ChartConsumer(std::shared_ptr<cyc::RecBuffer> buffer, QObject *parent)
    : QObject(parent), cyc::RecordConsumer(buffer), m_buffer(buffer)
{
}

ChartConsumer::~ChartConsumer() {
    stop();
}

SampleType ChartConsumer::mapFieldType(cyc::DataType cbfType) {
    switch (cbfType) {
    case cyc::DataType::dtInt8:   return SampleType::Int8;
    case cyc::DataType::dtUInt8:  return SampleType::UInt8;
    case cyc::DataType::dtInt16:  return SampleType::Int16;
    case cyc::DataType::dtUInt16: return SampleType::UInt16;
    case cyc::DataType::dtInt32:  return SampleType::Int32;
    case cyc::DataType::dtUInt32: return SampleType::UInt32;
    case cyc::DataType::dtInt64:  return SampleType::Int64;
    case cyc::DataType::dtUInt64: return SampleType::UInt64;
    case cyc::DataType::dtFloat:  return SampleType::Float32;
    case cyc::DataType::dtDouble: return SampleType::Float64;

    // Map boolean and character to 8-bit numerical formats for charting
    case cyc::DataType::dtBool:   return SampleType::UInt8;
    case cyc::DataType::dtChar:   return SampleType::Int8;

    // Fallback for dtUndefine, dtVoid, dtPtr, or any unknown types
    default:                      return SampleType::Float64;
    }
}

void ChartConsumer::onConsumeStart() {
    auto rule = m_buffer->getRule();

    // Colors are intentionally NOT set here.
    // ChartModel::addSeries() / addDigitalSeries() receive an invalid QColor
    // (the default) and auto-assign a theme-aware color from ChartTheme.
    // This ensures series colors are always correct for the active theme
    // (dark or light) without ChartConsumer needing to know anything about UI.
    //
    // To pin a specific color to a series, set cfg.color to a valid QColor
    // before emitting headerParsed(); ChartModel will then use it as-is and
    // will not recolor it on theme changes.

    for (const auto& attr : rule.getAttributes()) {
        const QString baseName = QString::fromStdString(attr.name);

        if (attr.hasBitFields()) {
            CbfSeriesConfig regCfg;
            regCfg.name  = baseName;
            regCfg.type  = mapFieldType(rule.getType(attr.id));
            regCfg.id    = attr.id;
            regCfg.index = 0;
            m_configs.append(regCfg);
            m_currentBatch.append({regCfg.name, makeSampleBuffer(regCfg.type)});

            for (int bitPos = 0; bitPos < static_cast<int>(attr.bitIds.size()); ++bitPos) {
                const int bid = attr.bitIds[bitPos];
                if (bid == 0) continue;

                CbfSeriesConfig bitCfg;
                bitCfg.name      = QString::fromStdString(cyc::PReg::getName(bid));
                bitCfg.type      = SampleType::UInt8;
                bitCfg.id        = attr.id;
                bitCfg.index     = 0;
                bitCfg.isDigital = true;
                bitCfg.bitPregId = bid;
                m_configs.append(bitCfg);
                m_currentBatch.append({bitCfg.name, makeSampleBuffer(SampleType::UInt8)});
            }
            continue;
        }

        size_t count = attr.count;
        if (count == 0) count = 1;

        for (size_t i = 0; i < count; ++i) {
            CbfSeriesConfig cfg;
            cfg.name  = (count > 1)
                           ? QString("%1[%2]").arg(baseName).arg(i)
                           : baseName;
            cfg.type  = mapFieldType(rule.getType(attr.id));
            cfg.id    = attr.id;
            cfg.index = static_cast<int>(i);
            m_configs.append(cfg);
            m_currentBatch.append({cfg.name, makeSampleBuffer(cfg.type)});
        }
    }

    m_lastFlushTime = std::chrono::steady_clock::now();

    emit headerParsed(m_configs);
}

void ChartConsumer::consumeRecord(const cyc::Record& rec) {
    for (int i = 0; i < m_configs.size(); ++i) {
        const auto& cfg = m_configs[i];

        if (cfg.isDigital) {
            auto& vec = std::get<QVector<uint8_t>>(m_currentBatch[i].samples);
            vec.append(rec.getBit(cfg.bitPregId) ? uint8_t(1) : uint8_t(0));
            continue;
        }

        std::visit([&rec, id = cfg.id, idx = cfg.index](auto& vec) {
            using T = std::decay_t<decltype(vec[0])>;
            if constexpr (std::is_same_v<T, int8_t>)        vec.append(rec.getInt8  (id, idx));
            else if constexpr (std::is_same_v<T, uint8_t>)  vec.append(rec.getUInt8 (id, idx));
            else if constexpr (std::is_same_v<T, int16_t>)  vec.append(rec.getInt16 (id, idx));
            else if constexpr (std::is_same_v<T, uint16_t>) vec.append(rec.getUInt16(id, idx));
            else if constexpr (std::is_same_v<T, int32_t>)  vec.append(rec.getInt32 (id, idx));
            else if constexpr (std::is_same_v<T, uint32_t>) vec.append(rec.getUInt32(id, idx));
            else if constexpr (std::is_same_v<T, int64_t>)  vec.append(rec.getInt64 (id, idx));
            else if constexpr (std::is_same_v<T, uint64_t>) vec.append(rec.getUInt64(id, idx));
            else if constexpr (std::is_same_v<T, float>)    vec.append(rec.getFloat (id, idx));
            else if constexpr (std::is_same_v<T, double>)   vec.append(rec.getDouble(id, idx));
            else vec.append(static_cast<T>(rec.getValue(id, idx)));
        }, m_currentBatch[i].samples);
    }

    m_recordsAccumulated++;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastFlushTime);

    if (elapsed.count() >= m_flushIntervalMs) {
        flushBatch();
    }
}

void ChartConsumer::flushBatch() {
    if (m_recordsAccumulated == 0) return;

    emit batchReady(m_currentBatch);

    m_currentBatch.clear();
    for (const auto& cfg : m_configs) {
        m_currentBatch.append({cfg.name, makeSampleBuffer(cfg.type)});
    }
    m_recordsAccumulated = 0;
    m_lastFlushTime = std::chrono::steady_clock::now();
}

void ChartConsumer::onConsumeStop() {
    // Flush remaining records
    if (m_recordsAccumulated > 0) {
        emit batchReady(m_currentBatch);
        m_currentBatch.clear();
        m_recordsAccumulated = 0;
    }
}
