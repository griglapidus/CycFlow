#include "ChartConsumer.h"
#include "Core/PAttr.h"
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
    const QList<QColor> colors = {Qt::green, Qt::cyan, Qt::magenta, Qt::yellow, Qt::red, Qt::white};
    int colorIndex = 0;

    for (const auto& attr : rule.getAttributes()) {
        CbfSeriesConfig cfg;
        cfg.name = QString::fromStdString(attr.name);
        cfg.color = colors[colorIndex++ % colors.size()];
        cfg.type = mapFieldType(rule.getType(attr.id));
        cfg.id = attr.id;
        m_configs.append(cfg);

        m_currentBatch.append({cfg.name, makeSampleBuffer(cfg.type)});
    }

    emit headerParsed(m_configs);
}

void ChartConsumer::consumeRecord(const cyc::Record& rec) {
    for (int i = 0; i < m_configs.size(); ++i) {
        const auto& cfg = m_configs[i];

        std::visit([&rec, id = cfg.id](auto& vec) {
            using T = std::decay_t<decltype(vec[0])>;
            if constexpr (std::is_same_v<T, int32_t>) {
                vec.append(rec.getInt32(id));
            } else if constexpr (std::is_same_v<T, double>) {
                vec.append(rec.getDouble(id));
            } else {
                vec.append(static_cast<T>(rec.getValue(id)));
            }
        }, m_currentBatch[i].samples);
    }

    m_recordsAccumulated++;

    if (m_recordsAccumulated >= m_batchSize) {
        emit batchReady(m_currentBatch);

        m_currentBatch.clear();
        for (const auto& cfg : m_configs) {
            m_currentBatch.append({cfg.name, makeSampleBuffer(cfg.type)});
        }
        m_recordsAccumulated = 0;
    }
}

void ChartConsumer::onConsumeStop() {
    // Flush remaining records
    if (m_recordsAccumulated > 0) {
        emit batchReady(m_currentBatch);
        m_currentBatch.clear();
        m_recordsAccumulated = 0;
    }
}
