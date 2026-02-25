#ifndef TCPSESSIONWIDGET_H
#define TCPSESSIONWIDGET_H

#include <QWidget>
#include <QTimer>
#include "AppConfig.h"
#include "../ChartWidget/ChartWidget.h"
#include "ChartConsumer.h"
#include "Tcp/TcpDataReceiver.h"

class TcpSessionWidget : public QWidget {
    Q_OBJECT
public:
    explicit TcpSessionWidget(const ConnectionConfig& config, QWidget *parent = nullptr);
    ~TcpSessionWidget() override;

    void startConnection();
    void stopConnection();

    // Возвращает текст правила, если соединение было установлено
    QString getRecRuleText() const;

signals:
    void statusChanged(const QString& status);

private slots:
    void tryConnect();
    void checkStatus();

private:
    ConnectionConfig m_config;
    ChartWidget* m_chartWidget = nullptr;
    cyc::TcpDataReceiver* m_receiver = nullptr;
    ChartConsumer* m_consumer = nullptr;

    QTimer* m_connectTimer = nullptr;
    QTimer* m_statusTimer = nullptr;

    bool m_active = false;
    QString m_lastStatus;
    QString m_ruleTextCache;
};

#endif // TCPSESSIONWIDGET_H
