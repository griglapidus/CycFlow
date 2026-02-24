#include "MainWindow.h"
#include "CbfReader.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("CBF Viewer");
    resize(1280, 640);

    setupMenu();
    createNewChartWidget();
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupMenu()
{
    QMenu *fileMenu = menuBar()->addMenu("&File");

    QAction *openAction = fileMenu->addAction("&Open...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onActionOpenTriggered);

    fileMenu->addSeparator();

    QAction *exitAction = fileMenu->addAction("E&xit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
}

void MainWindow::createNewChartWidget()
{
    if (m_chartWidget) {
        m_chartWidget->deleteLater();
    }

    m_chartWidget = new ChartWidget(this);
    setCentralWidget(m_chartWidget);
}

void MainWindow::onActionOpenTriggered()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        "Open CBF File",
        "",
        "Cyc Binary Format (*.cbf);;All Files (*)"
        );

    if (!filePath.isEmpty()) {
        openFile(filePath);
    }
}

void MainWindow::openFile(const QString &filePath)
{
    // Recreate the widget to ensure a clean model state
    createNewChartWidget();

    setWindowTitle("CBF Viewer - " + filePath);

    ChartModel *model = m_chartWidget->model();

    QThread *readerThread = new QThread(this);
    CbfReader *reader = new CbfReader(filePath);
    reader->moveToThread(readerThread);

    connect(reader, &CbfReader::headerParsed, model, [model](const QVector<CbfSeriesConfig>& configs) {
        for (const auto& cfg : configs) {
            model->addSeries(cfg.name, cfg.color, cfg.type);
        }
    });

    connect(reader, &CbfReader::batchReady, model, &ChartModel::appendBatch);

    connect(reader, &CbfReader::error, this, [this](const QString& msg) {
        QMessageBox::critical(this, "Reader Error", msg);
    });

    connect(readerThread, &QThread::started, reader, &CbfReader::process);
    connect(reader, &CbfReader::finished, readerThread, &QThread::quit);
    connect(reader, &CbfReader::finished, reader, &QObject::deleteLater);
    connect(readerThread, &QThread::finished, readerThread, &QObject::deleteLater);

    readerThread->start();
}
