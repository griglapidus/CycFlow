#ifndef CONNECTIONDIALOG_H
#define CONNECTIONDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include "AppConfig.h"

class ConnectionDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConnectionDialog(QWidget *parent = nullptr);
    QList<ConnectionConfig> getConfigs() const;

private slots:
    void addRow();
    void removeRow();

private:
    QTableWidget* m_table = nullptr;
};

#endif // CONNECTIONDIALOG_H
