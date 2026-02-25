#include "MainWindow.h"
#include <QToolBar>
#include <QSplitter>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QMdiSubWindow>
#include <QSettings>
#include <QInputDialog>
#include <QDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>

#include "Tcp/TcpServiceClient.h"

// --- Custom Dialog for Host/Port Discovery ---
class DiscoveryDialog : public QDialog {
public:
    DiscoveryDialog(const QString& defaultHost, uint16_t defaultPort, const QStringList& history, QWidget* parent = nullptr)
        : QDialog(parent) {
        setWindowTitle("Discover Buffers");
        resize(300, 100);
        auto* layout = new QFormLayout(this);

        m_combo = new QComboBox(this);
        m_combo->setEditable(true);
        m_combo->addItems(history);

        // Pre-fill with the currently selected row's data
        QString current = QString("%1:%2").arg(defaultHost).arg(defaultPort);
        m_combo->setCurrentText(current);

        layout->addRow("Server (host:port):", m_combo);

        auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(btnBox);
    }

    QString getHost() const {
        QString text = m_combo->currentText().trimmed();
        int idx = text.lastIndexOf(':');
        return (idx == -1) ? text : text.left(idx);
    }

    uint16_t getPort() const {
        QString text = m_combo->currentText().trimmed();
        int idx = text.lastIndexOf(':');
        return (idx == -1) ? 5000 : text.mid(idx + 1).toUShort(); // Default to 5000 if no port provided
    }

private:
    QComboBox* m_combo;
};
// ---------------------------------------------


MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("CycLib MDI Streaming Viewer");
    resize(1400, 900);
    setupUi();

    loadSettings();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveSettings();
    stopAll(); // Ensure all network threads are stopped properly
    QMainWindow::closeEvent(event);
}

void MainWindow::setupUi() {
    // 1. Toolbar
    QToolBar* tb = addToolBar("Main Controls");
    tb->setMovable(false);

    QAction* actStartAll = tb->addAction("Start All");
    QAction* actStopAll = tb->addAction("Stop All");
    tb->addSeparator();
    QAction* actAddRow = tb->addAction("Add Connection");
    QAction* actRemoveRow = tb->addAction("Remove Connection");
    tb->addSeparator();
    QAction* actDiscover = tb->addAction("Discover Buffers");
    QAction* actShowRule = tb->addAction("Show RecRule");

    connect(actStartAll, &QAction::triggered, this, &MainWindow::startAll);
    connect(actStopAll, &QAction::triggered, this, &MainWindow::stopAll);
    connect(actAddRow, &QAction::triggered, this, &MainWindow::addRow);
    connect(actRemoveRow, &QAction::triggered, this, &MainWindow::removeRow);
    connect(actDiscover, &QAction::triggered, this, &MainWindow::discoverBuffers);
    connect(actShowRule, &QAction::triggered, this, &MainWindow::showRecRule);

    // 2. Central Widget (Splitter)
    auto* splitter = new QSplitter(Qt::Vertical, this);
    setCentralWidget(splitter);

    // 3. Table
    m_table = new QTableWidget(0, 6, this);
    m_table->setHorizontalHeaderLabels({"En", "View", "Host", "Port", "Buffer", "Status"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 40);
    m_table->setColumnWidth(1, 40);
    m_table->setColumnWidth(2, 120);
    m_table->setColumnWidth(3, 80);
    m_table->setColumnWidth(4, 150);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);

    connect(m_table, &QTableWidget::itemChanged, this, &MainWindow::onTableItemChanged);

    splitter->addWidget(m_table);

    // 4. MDI Area
    m_mdiArea = new QMdiArea(this);
    m_mdiArea->setViewMode(QMdiArea::TabbedView);
    m_mdiArea->setTabsClosable(false);
    splitter->addWidget(m_mdiArea);

    splitter->setSizes({200, 700});
}

void MainWindow::addRow() {
    QString defBuffer = QString("Buffer_%1").arg(m_table->rowCount() + 1);
    addConnectionRow(false, true, "127.0.0.1", 5000, defBuffer);
}

void MainWindow::addConnectionRow(bool enabled, bool visible, const QString& host, uint16_t port, const QString& bufferName) {
    m_isUpdatingTable = true;
    int r = m_table->rowCount();
    m_table->insertRow(r);

    auto* itemEn = new QTableWidgetItem();
    itemEn->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    itemEn->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    m_table->setItem(r, 0, itemEn);

    auto* itemVw = new QTableWidgetItem();
    itemVw->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    itemVw->setCheckState(visible ? Qt::Checked : Qt::Unchecked);
    m_table->setItem(r, 1, itemVw);

    m_table->setItem(r, 2, new QTableWidgetItem(host));
    m_table->setItem(r, 3, new QTableWidgetItem(QString::number(port)));
    m_table->setItem(r, 4, new QTableWidgetItem(bufferName));

    auto* itemStatus = new QTableWidgetItem("Stopped");
    itemStatus->setFlags(Qt::ItemIsEnabled);
    m_table->setItem(r, 5, itemStatus);

    m_isUpdatingTable = false;

    if (enabled) {
        setRowEnabled(r, true);
    }
    if (!visible) {
        setRowVisible(r, false);
    }
}

void MainWindow::removeRow() {
    int r = m_table->currentRow();
    if (r < 0) return;

    QTableWidgetItem* keyItem = m_table->item(r, 0);

    if (m_sessionMap.contains(keyItem)) {
        SessionInfo info = m_sessionMap.take(keyItem);
        if (info.session) {
            info.session->stopConnection();
            info.session->deleteLater();
        }
    }

    m_table->removeRow(r);
}

void MainWindow::onTableItemChanged(QTableWidgetItem* item) {
    if (m_isUpdatingTable) return;

    int r = item->row();
    int c = item->column();

    if (c == 0) {
        setRowEnabled(r, item->checkState() == Qt::Checked);
    } else if (c == 1) {
        setRowVisible(r, item->checkState() == Qt::Checked);
    }
}

void MainWindow::setRowEnabled(int row, bool enable) {
    QTableWidgetItem* keyItem = m_table->item(row, 0);
    SessionInfo& info = m_sessionMap[keyItem];

    if (enable) {
        if (!info.session) {
            ConnectionConfig cfg;
            cfg.host = m_table->item(row, 2)->text();
            cfg.port = m_table->item(row, 3)->text().toUShort();
            cfg.bufferName = m_table->item(row, 4)->text();

            info.session = new TcpSessionWidget(cfg, this);

            connect(info.session, &TcpSessionWidget::statusChanged, this, [this, keyItem](const QString& st){
                int r = keyItem->row();
                if (r >= 0 && m_table->item(r, 5)) {
                    m_table->item(r, 5)->setText(st);
                }
            });

            info.subWindow = m_mdiArea->addSubWindow(info.session);
        }

        info.session->startConnection();
        info.subWindow->setVisible(m_table->item(row, 1)->checkState() == Qt::Checked);

        for(int i = 2; i <= 4; ++i) {
            m_table->item(row, i)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        }
    } else {
        if (info.session) {
            info.session->stopConnection();
        }

        for(int i = 2; i <= 4; ++i) {
            m_table->item(row, i)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        }
    }
}

void MainWindow::setRowVisible(int row, bool visible) {
    QTableWidgetItem* keyItem = m_table->item(row, 0);
    if (m_sessionMap.contains(keyItem)) {
        SessionInfo info = m_sessionMap.value(keyItem);
        if (info.subWindow) {
            info.subWindow->setVisible(visible);
            if (visible) m_mdiArea->setActiveSubWindow(info.subWindow);
        }
    }
}

void MainWindow::startAll() {
    m_isUpdatingTable = true;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        m_table->item(r, 0)->setCheckState(Qt::Checked);
        setRowEnabled(r, true);
    }
    m_isUpdatingTable = false;
}

void MainWindow::stopAll() {
    m_isUpdatingTable = true;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        m_table->item(r, 0)->setCheckState(Qt::Unchecked);
        setRowEnabled(r, false);
    }
    m_isUpdatingTable = false;
}

void MainWindow::discoverBuffers() {
    int r = m_table->currentRow();
    if (r < 0) {
        QMessageBox::warning(this, "Discovery Error", "Please select a connection row first.");
        return;
    }

    // Prevent discovering while connection is active (since it modifies row data)
    if (m_table->item(r, 0)->checkState() == Qt::Checked) {
        QMessageBox::warning(this, "Discovery Error", "Cannot discover buffers while the connection is active. Please disable it first.");
        return;
    }

    QString currentHost = m_table->item(r, 2)->text();
    uint16_t currentPort = m_table->item(r, 3)->text().toUShort();

    DiscoveryDialog dlg(currentHost, currentPort, m_discoveryHistory, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    QString host = dlg.getHost();
    uint16_t port = dlg.getPort();

    if (host.isEmpty() || port == 0) {
        QMessageBox::warning(this, "Input Error", "Invalid host or port provided.");
        return;
    }

    // Manage History: keep maximum 10 unique elements, most recent at the top
    QString historyEntry = QString("%1:%2").arg(host).arg(port);
    m_discoveryHistory.removeAll(historyEntry);
    m_discoveryHistory.prepend(historyEntry);
    while (m_discoveryHistory.size() > 10) {
        m_discoveryHistory.removeLast();
    }

    // Fetch available buffers from the server
    std::vector<std::string> buffers = cyc::TcpServiceClient::requestBufferList(host.toStdString(), port);

    if (buffers.empty()) {
        QMessageBox::warning(this, "Discovery Failed", "No buffers found or connection refused by the server.");
        return;
    }

    QStringList bufferList;
    for (const auto& b : buffers) {
        bufferList.append(QString::fromStdString(b));
    }

    bool ok;
    QString selectedBuffer = QInputDialog::getItem(this, "Select Buffer", "Available buffers on server:", bufferList, 0, false, &ok);

    if (ok && !selectedBuffer.isEmpty()) {
        m_table->item(r, 2)->setText(host);
        m_table->item(r, 3)->setText(QString::number(port));
        m_table->item(r, 4)->setText(selectedBuffer);
    }
}

void MainWindow::showRecRule() {
    int r = m_table->currentRow();
    if (r < 0) {
        QMessageBox::warning(this, "Info", "Please select a connection row first.");
        return;
    }

    QTableWidgetItem* keyItem = m_table->item(r, 0);
    if (m_sessionMap.contains(keyItem) && m_sessionMap[keyItem].session) {
        QString rule = m_sessionMap[keyItem].session->getRecRuleText();
        if (rule.isEmpty()) {
            QMessageBox::information(this, "RecRule", "No rule received yet. Ensure the connection is active and data is streaming.");
        } else {
            QMessageBox::information(this, "RecRule for " + m_table->item(r, 4)->text(), rule);
        }
    } else {
        QMessageBox::information(this, "RecRule", "Session is not active. Please enable it first.");
    }
}

void MainWindow::saveSettings() {
    QSettings settings;

    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.setValue("discoveryHistory", m_discoveryHistory);

    settings.beginWriteArray("Connections");
    for (int i = 0; i < m_table->rowCount(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("enabled", m_table->item(i, 0)->checkState() == Qt::Checked);
        settings.setValue("visible", m_table->item(i, 1)->checkState() == Qt::Checked);
        settings.setValue("host", m_table->item(i, 2)->text());
        settings.setValue("port", m_table->item(i, 3)->text().toUShort());
        settings.setValue("bufferName", m_table->item(i, 4)->text());
    }
    settings.endArray();
}

void MainWindow::loadSettings() {
    QSettings settings;

    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    m_discoveryHistory = settings.value("discoveryHistory").toStringList();

    int size = settings.beginReadArray("Connections");
    if (size == 0) {
        addRow();
    } else {
        for (int i = 0; i < size; ++i) {
            settings.setArrayIndex(i);
            bool enabled = settings.value("enabled", false).toBool();
            bool visible = settings.value("visible", true).toBool();
            QString host = settings.value("host", "127.0.0.1").toString();
            uint16_t port = static_cast<uint16_t>(settings.value("port", 5000).toUInt());
            QString bufferName = settings.value("bufferName", "").toString();

            addConnectionRow(enabled, visible, host, port, bufferName);
        }
    }
    settings.endArray();
}
