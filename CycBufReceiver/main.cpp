#include <QApplication>
#include "MainWindow.h"
#include "AppConfig.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle("Fusion");

    QCoreApplication::setOrganizationName("CycTools");
    QCoreApplication::setApplicationName("CycBufReceiver");

    // Register custom types for signal/slot queues across threads
    qRegisterMetaType<QList<SeriesBatch>>("QList<SeriesBatch>");
    qRegisterMetaType<QVector<CbfSeriesConfig>>("QVector<CbfSeriesConfig>");

    MainWindow w;
    w.show();

    app.setApplicationName("CycBufReceiver");
    app.setApplicationVersion(APP_VERSION);
    app.setWindowIcon(QIcon(":/icons/icon_dark"));

    return app.exec();
}
