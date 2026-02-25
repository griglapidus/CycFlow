#include "DataGenerator.h"

#include <QRandomGenerator>
#include <QTimer>
#include <cmath>

DataGenerator::DataGenerator(QObject *parent) : QObject(parent) {}

void DataGenerator::setConfigs(const QVector<SeriesConfig> &configs) { m_configs = configs; }
void DataGenerator::setBatchSize(int n)   { m_batchSize  = qMax(1, n);  }
void DataGenerator::setIntervalMs(int ms) { m_intervalMs = qMax(1, ms); }

void DataGenerator::start()
{
    if (m_running) return;
    m_running = true;

    auto *timer = new QTimer(this);
    timer->setInterval(m_intervalMs);
    timer->setTimerType(Qt::PreciseTimer);

    connect(timer, &QTimer::timeout, this, [this]() {
        if (!m_running) return;

        QList<SeriesBatch> batch;
        batch.reserve(m_configs.size());

        for (auto &cfg : m_configs) {
            QVector<float> data(m_batchSize);
            for (int i = 0; i < m_batchSize; ++i) {
                data[i] = cfg.bias
                          + cfg.amp * std::sin((m_tick + i) * cfg.freq)
                          + 0.05f * static_cast<float>(
                                QRandomGenerator::global()->generateDouble());
            }
            batch.append(SeriesBatch{ cfg.name, std::move(data) });
        }

        m_tick += m_batchSize;
        emit batchReady(std::move(batch));
    });

    timer->start();
}

void DataGenerator::stop()
{
    m_running = false;
    for (auto *t : findChildren<QTimer *>()) t->stop();
}
