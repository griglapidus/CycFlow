#include "CbfReader.h"
#include "Core/Record.h"
#include "Core/PAttr.h"
#include <QVariant>
#include <vector>
#include "Cbf/CbfFile.h"

CbfReader::CbfReader(const QString &filePath, QObject *parent)
    : QObject(parent), m_filePath(filePath)
{
}

SampleType CbfReader::mapFieldType(cyc::DataType cbfType)
{
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

    case cyc::DataType::dtBool:   return SampleType::UInt8;
    case cyc::DataType::dtChar:   return SampleType::Int8;

    default:                      return SampleType::Float64;
    }
}

void CbfReader::process()
{
    cyc::CbfFile file;
    if (!file.open(m_filePath.toStdString(), cyc::CbfMode::Read)) {
        emit error("Failed to open CBF file: " + m_filePath);
        emit finished();
        return;
    }

    cyc::CbfSectionHeader header;
    cyc::RecRule rule;
    QVector<CbfSeriesConfig> seriesConfigs;
    bool hasRule = false;

    while (file.readSectionHeader(header)) {

        if (header.type == static_cast<uint8_t>(cyc::CbfSectionType::Header)) {
            if (file.readRule(header, rule)) {
                hasRule = true;

                const QList<QColor> colors = {Qt::green, Qt::cyan, Qt::magenta, Qt::yellow, Qt::red, Qt::white};
                int colorIndex = 0;

                const auto& attributes = rule.getAttributes();
                for (const auto& attr : attributes) {
                    QString baseName = QString::fromStdString(attr.name);

                    size_t count = attr.count;
                    if (count == 0) count = 1;

                    for (size_t i = 0; i < count; ++i) {
                        CbfSeriesConfig cfg;

                        if (count > 1) {
                            cfg.name = QString("%1[%2]").arg(baseName).arg(i);
                        } else {
                            cfg.name = baseName;
                        }

                        cfg.color = colors[colorIndex++ % colors.size()];
                        cfg.type = mapFieldType(rule.getType(attr.id));
                        cfg.id = attr.id;
                        cfg.index = static_cast<int>(i);

                        seriesConfigs.append(cfg);
                    }
                }

                emit headerParsed(seriesConfigs);
            } else {
                emit error("Failed to parse RecRule from Header section.");
                break;
            }
        }
        else if (header.type == static_cast<uint8_t>(cyc::CbfSectionType::Data)) {
            if (!hasRule) {
                emit error("Encountered Data section before Header section.");
                break;
            }

            std::vector<uint8_t> rawBuffer(rule.getRecSize());
            cyc::Record rec(rule, rawBuffer.data());

            QList<SeriesBatch> currentBatch;
            for (const auto& cfg : seriesConfigs) {
                currentBatch.append({cfg.name, makeSampleBuffer(cfg.type)});
            }

            int recordsRead = 0;

            while (file.readRecord(rec)) {
                for (int i = 0; i < seriesConfigs.size(); ++i) {
                    const auto& cfg = seriesConfigs[i];

                    std::visit([&rec, id = cfg.id, idx = cfg.index](auto& vec) {
                        using T = std::decay_t<decltype(vec[0])>;

                        if constexpr (std::is_same_v<T, int8_t>) {
                            vec.append(rec.getInt8(id, idx));
                        } else if constexpr (std::is_same_v<T, uint8_t>) {
                            vec.append(rec.getUInt8(id, idx));
                        } else if constexpr (std::is_same_v<T, int16_t>) {
                            vec.append(rec.getInt16(id, idx));
                        } else if constexpr (std::is_same_v<T, uint16_t>) {
                            vec.append(rec.getUInt16(id, idx));
                        } else if constexpr (std::is_same_v<T, int32_t>) {
                            vec.append(rec.getInt32(id, idx));
                        } else if constexpr (std::is_same_v<T, uint32_t>) {
                            vec.append(rec.getUInt32(id, idx));
                        } else if constexpr (std::is_same_v<T, int64_t>) {
                            vec.append(rec.getInt64(id, idx));
                        } else if constexpr (std::is_same_v<T, uint64_t>) {
                            vec.append(rec.getUInt64(id, idx));
                        } else if constexpr (std::is_same_v<T, float>) {
                            vec.append(rec.getFloat(id, idx));
                        } else if constexpr (std::is_same_v<T, double>) {
                            vec.append(rec.getDouble(id, idx));
                        } else {
                            vec.append(static_cast<T>(rec.getValue(id, idx)));
                        }
                    }, currentBatch[i].samples);
                }

                recordsRead++;

                if (recordsRead >= m_batchSize) {
                    emit batchReady(currentBatch);

                    for (int i = 0; i < seriesConfigs.size(); ++i) {
                        currentBatch[i].samples = makeSampleBuffer(seriesConfigs[i].type);
                    }
                    recordsRead = 0;
                }
            }

            if (recordsRead > 0) {
                emit batchReady(currentBatch);
            }
        }
        else {
            file.skipSection(header);
        }
    }

    emit finished();
}
