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
    // Map known Cyc data types to Chart SampleType
    // Expand this switch if other DataTypes are available in cyc library
    if (cbfType == cyc::DataType::dtInt32) {
        return SampleType::Int32;
    }
    if (cbfType == cyc::DataType::dtDouble) {
        return SampleType::Float64;
    }

    // Default fallback
    return SampleType::Float64;
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

                const QList<QColor> colors = {Qt::green, Qt::cyan, Qt::magenta, Qt::yellow};
                int colorIndex = 0;

                const auto& attributes = rule.getAttributes();
                for (const auto& attr : attributes) {
                    CbfSeriesConfig cfg;
                    cfg.name = QString::fromStdString(attr.name);
                    cfg.color = colors[colorIndex++ % colors.size()];
                    cfg.type = mapFieldType(rule.getType(attr.id));
                    cfg.id = attr.id;

                    seriesConfigs.append(cfg);
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

            // Allocate buffer and initialize Record based on test_Cbf.cpp logic
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

                    std::visit([&rec, id = cfg.id](auto& vec) {
                        using T = std::decay_t<decltype(vec[0])>;

                        // Use specific getters if known, otherwise fallback to getValue
                        if constexpr (std::is_same_v<T, int32_t>) {
                            vec.append(rec.getInt32(id));
                        } else if constexpr (std::is_same_v<T, double>) {
                            vec.append(rec.getDouble(id));
                        } else {
                            vec.append(static_cast<T>(rec.getValue(id)));
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
