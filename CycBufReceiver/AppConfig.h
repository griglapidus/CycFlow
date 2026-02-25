#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QString>
#include <QColor>
#include "../ChartWidget/ChartDefs.h"

struct ConnectionConfig {
    QString host;
    uint16_t port;
    QString bufferName;
};

struct CbfSeriesConfig {
    QString name;
    QColor color;
    SampleType type;
    int id;
};

#endif // APPCONFIG_H
