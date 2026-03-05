#ifndef CHARTCONSUMER_H
#define CHARTCONSUMER_H

#include <QObject>
#include <QVector>
#include <QList>
#include <QTimer>
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

    /**
     * @brief Sets the maximum interval between UI updates (in ms).
     *
     * The consumer will emit batchReady() no less frequently than this,
     * as long as there is at least one accumulated record.
     */
    void setFlushIntervalMs(int ms) { m_flushIntervalMs = ms; }

signals:
    void headerParsed(QVector<CbfSeriesConfig> configs);
    void batchReady(QList<SeriesBatch> batch);

protected:
    void onConsumeStart() override;
    void consumeRecord(const cyc::Record& rec) override;
    void onConsumeStop() override;

private:
    SampleType mapFieldType(cyc::DataType cbfType);
    void flushBatch();

    std::shared_ptr<cyc::RecBuffer> m_buffer;
    QVector<CbfSeriesConfig> m_configs;
    QList<SeriesBatch> m_currentBatch;

    int m_recordsAccumulated = 0;
    int m_flushIntervalMs = 25;

    /// Monotonic clock snapshot of the last flush (or start).
    std::chrono::steady_clock::time_point m_lastFlushTime;
};

#endif // CHARTCONSUMER_H
