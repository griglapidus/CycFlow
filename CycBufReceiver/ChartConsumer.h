#ifndef CHARTCONSUMER_H
#define CHARTCONSUMER_H

#include <QObject>
#include <QVector>
#include <QList>
#include <memory>

#include "AppConfig.h"
#include "../ChartWidget/ChartDefs.h"
#include "RecordConsumer.h"
#include "Core/Record.h"
#include "Core/RecBuffer.h"

class ChartConsumer : public QObject, public cyc::RecordConsumer {
    Q_OBJECT
public:
    explicit ChartConsumer(std::shared_ptr<cyc::RecBuffer> buffer, QObject *parent = nullptr);
    ~ChartConsumer() override;

signals:
    void headerParsed(QVector<CbfSeriesConfig> configs);
    void batchReady(QList<SeriesBatch> batch);

protected:
    void onConsumeStart() override;
    void consumeRecord(const cyc::Record& rec) override;
    void onConsumeStop() override;

private:
    SampleType mapFieldType(cyc::DataType cbfType);

    std::shared_ptr<cyc::RecBuffer> m_buffer;
    QVector<CbfSeriesConfig> m_configs;
    QList<SeriesBatch> m_currentBatch;

    int m_recordsAccumulated = 0;
    const int m_batchSize = 1000;
};

#endif // CHARTCONSUMER_H
