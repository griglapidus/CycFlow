#include <QApplication>
#include "MainWindow.h"
#include "AppConfig.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle("Fusion");

    // Configure QSettings path
    QCoreApplication::setOrganizationName("CycLibTools");
    QCoreApplication::setApplicationName("MDIStreamingViewer");

    // Register custom types for signal/slot queues across threads
    qRegisterMetaType<QList<SeriesBatch>>("QList<SeriesBatch>");
    qRegisterMetaType<QVector<CbfSeriesConfig>>("QVector<CbfSeriesConfig>");

    MainWindow w;
    w.show();

    return app.exec();
}
