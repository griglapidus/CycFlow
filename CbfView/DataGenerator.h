#ifndef DATAGENERATOR_H
#define DATAGENERATOR_H

#include <QVector>
#include <QObject>

class DataGenerator : public QObject
{
    Q_OBJECT
public:
    struct SeriesConfig {
        int   index;
        float freq;
        float amp;
        float bias;
    };

    explicit DataGenerator(QObject *parent = nullptr);
    void setConfigs(const QVector<SeriesConfig> &configs);
    void setBatchSize(int n);
    void setIntervalMs(int ms);

public slots:
    void start();
    void stop();

signals:
    void batchReady(int seriesIndex, QVector<float> samples);

private:
    QVector<SeriesConfig> m_configs;
    int  m_batchSize  = 10;
    int  m_intervalMs = 50;
    int  m_tick       = 0;
    bool m_running    = false;
};

#endif // DATAGENERATOR_H
