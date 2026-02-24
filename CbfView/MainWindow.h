#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include "ChartWidget.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void openFile(const QString &filePath);

private slots:
    void onActionOpenTriggered();

private:
    void setupMenu();
    void createNewChartWidget();

    ChartWidget *m_chartWidget = nullptr;
};

#endif // MAINWINDOW_H
