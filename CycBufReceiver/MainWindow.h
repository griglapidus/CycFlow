#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMdiArea>
#include <QTableWidget>
#include <QMap>
#include <QCloseEvent>
#include <QStringList>
#include "AppConfig.h"
#include "TcpSessionWidget.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void addRow();
    void removeRow();
    void startAll();
    void stopAll();
    void showRecRule();
    void discoverBuffers();
    void onTableItemChanged(QTableWidgetItem* item);

private:
    void setupUi();
    void setRowEnabled(int row, bool enable);
    void setRowVisible(int row, bool visible);

    void addConnectionRow(bool enabled, bool visible, const QString& host, uint16_t port, const QString& bufferName);

    void saveSettings();
    void loadSettings();

    struct SessionInfo {
        TcpSessionWidget* session = nullptr;
        QMdiSubWindow* subWindow = nullptr;
    };

    QTableWidget* m_table = nullptr;
    QMdiArea* m_mdiArea = nullptr;

    bool m_isUpdatingTable = false;
    QMap<QTableWidgetItem*, SessionInfo> m_sessionMap;

    // List to keep the history of the last 10 discovered host:port combinations
    QStringList m_discoveryHistory;
};

#endif // MAINWINDOW_H
