#include "ConnectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>

ConnectionDialog::ConnectionDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Network Connections Configuration");
    resize(600, 300);

    auto* layout = new QVBoxLayout(this);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({"Alias / Name", "Host", "Port", "Buffer Name"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(m_table);

    auto* btnLayout = new QHBoxLayout();
    auto* btnAdd = new QPushButton("Add Connection");
    auto* btnRemove = new QPushButton("Remove Selected");
    auto* btnStart = new QPushButton("Start All");

    btnLayout->addWidget(btnAdd);
    btnLayout->addWidget(btnRemove);
    btnLayout->addStretch();
    btnLayout->addWidget(btnStart);
    layout->addLayout(btnLayout);

    connect(btnAdd, &QPushButton::clicked, this, &ConnectionDialog::addRow);
    connect(btnRemove, &QPushButton::clicked, this, &ConnectionDialog::removeRow);
    connect(btnStart, &QPushButton::clicked, this, &QDialog::accept);

    // Default row for convenience
    addRow();
    m_table->setItem(0, 0, new QTableWidgetItem("Engine 1"));
    m_table->setItem(0, 1, new QTableWidgetItem("127.0.0.1"));
    m_table->setItem(0, 2, new QTableWidgetItem("5000"));
    m_table->setItem(0, 3, new QTableWidgetItem("StreamA"));
}

void ConnectionDialog::addRow() {
    int row = m_table->rowCount();
    m_table->insertRow(row);
}

void ConnectionDialog::removeRow() {
    int row = m_table->currentRow();
    if (row >= 0) m_table->removeRow(row);
}

QList<ConnectionConfig> ConnectionDialog::getConfigs() const {
    QList<ConnectionConfig> configs;
    for (int i = 0; i < m_table->rowCount(); ++i) {
        ConnectionConfig cfg;
        cfg.name = m_table->item(i, 0) ? m_table->item(i, 0)->text() : "Unknown";
        cfg.host = m_table->item(i, 1) ? m_table->item(i, 1)->text() : "127.0.0.1";
        cfg.port = m_table->item(i, 2) ? m_table->item(i, 2)->text().toUShort() : 0;
        cfg.bufferName = m_table->item(i, 3) ? m_table->item(i, 3)->text() : "";

        if (!cfg.host.isEmpty() && cfg.port > 0 && !cfg.bufferName.isEmpty()) {
            configs.append(cfg);
        }
    }
    return configs;
}
