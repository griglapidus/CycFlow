#ifndef CBFREADER_H
#define CBFREADER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QColor>
#include "../CycBufReceiver/AppConfig.h"
#include "../ChartWidget/ChartDefs.h"
#include "Core/Common.h"

class CbfReader : public QObject
{
    Q_OBJECT
public:
    explicit CbfReader(const QString &filePath, QObject *parent = nullptr);

public slots:
    void process();

signals:
    void headerParsed(QVector<CbfSeriesConfig> configs);
    void batchReady(QList<SeriesBatch> batch);
    void error(QString msg);
    void finished();

private:
    QString m_filePath;
    int m_batchSize = 2000;

    SampleType mapFieldType(cyc::DataType cbfType);
};

#endif // CBFREADER_H
