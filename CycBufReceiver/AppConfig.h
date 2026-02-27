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
    QString    name;
    QColor     color;
    SampleType type;
    int        id    = 0;   ///< PReg ID родительского поля (для аналоговых серий)
    int        index = 0;   ///< Индекс элемента массива

    bool    isDigital = false; ///< true → бит-серия, читается через rec.getBit()
    int     bitPregId = 0;     ///< PReg ID именованного бита
};

#endif // APPCONFIG_H
