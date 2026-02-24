#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>

#include "MainWindow.h"
#include "ChartDefs.h"
#include "CbfReader.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle("Fusion");

    // Register custom types for signal/slot queues across threads
    qRegisterMetaType<QList<SeriesBatch>>("QList<SeriesBatch>");
    qRegisterMetaType<QVector<CbfSeriesConfig>>("QVector<CbfSeriesConfig>");

    QCommandLineParser parser;
    parser.setApplicationDescription("Application for viewing Cyc Binary Format files");
    parser.addHelpOption();

    // Positional argument for the file path
    parser.addPositionalArgument("file", "The CBF file to open.");
    parser.process(app);

    MainWindow mainWindow;
    mainWindow.show();

    // Check if a file was passed via command line
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        mainWindow.openFile(args.first());
    }

    return app.exec();
}
