#include "TcpSessionWidget.h"
#include <QVBoxLayout>

TcpSessionWidget::TcpSessionWidget(const ConnectionConfig& config, QWidget *parent)
    : QWidget(parent), m_config(config)
{
    setWindowTitle(QString("%1 [%2:%3]").arg(config.bufferName, config.host, config.port));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_chartWidget = new ChartWidget(this);
    layout->addWidget(m_chartWidget);

    m_connectTimer = new QTimer(this);
    connect(m_connectTimer, &QTimer::timeout, this, &TcpSessionWidget::tryConnect);

    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &TcpSessionWidget::checkStatus);
    m_statusTimer->start(1000);
}

TcpSessionWidget::~TcpSessionWidget() {
    stopConnection();
}

void TcpSessionWidget::startConnection() {
    if (m_active) return;
    m_active = true;
    m_ruleTextCache.clear();

    if (!m_receiver) {
        m_receiver = new cyc::TcpDataReceiver();
    }

    emit statusChanged("Connecting...");
    m_connectTimer->start(2000);
    tryConnect();
}

void TcpSessionWidget::stopConnection() {
    m_active = false;
    m_connectTimer->stop();

    if (m_consumer) {
        m_consumer->stop();
        delete m_consumer;
        m_consumer = nullptr;
    }
    if (m_receiver) {
        m_receiver->stop();
        delete m_receiver;
        m_receiver = nullptr;
    }

    m_lastStatus = "Stopped";
    emit statusChanged(m_lastStatus);
}

QString TcpSessionWidget::getRecRuleText() const {
    return m_ruleTextCache;
}

void TcpSessionWidget::tryConnect() {
    if (m_receiver && m_receiver->isConnected()) return;

    emit statusChanged("Connecting...");

    if (m_receiver->connect(m_config.host.toStdString(), m_config.port, m_config.bufferName.toStdString())) {
        m_connectTimer->stop();

        if (m_consumer) {
            m_consumer->stop();
            m_consumer->deleteLater();
            m_consumer = nullptr;
        }

        auto buffer = m_receiver->getBuffer();
        if (buffer) {
            m_ruleTextCache = QString::fromStdString(buffer->getRule().toText());

            ChartModel* model = m_chartWidget->model();

            model->clearAll();

            m_consumer = new ChartConsumer(buffer);

            QObject::connect(m_consumer, &ChartConsumer::headerParsed, model,
                             [model](const QVector<CbfSeriesConfig>& configs) {
                                 for (const auto& cfg : configs) {
                                     if (cfg.isDigital)
                                         model->addDigitalSeries(cfg.name, cfg.color);
                                     else
                                         model->addSeries(cfg.name, cfg.color, cfg.type);
                                 }
                             }, Qt::QueuedConnection);

            QObject::connect(m_consumer, &ChartConsumer::batchReady, model, &ChartModel::appendBatch, Qt::QueuedConnection);

            m_consumer->start();

            m_lastStatus = "Connected";
            emit statusChanged(m_lastStatus);
        }
    } else {
        m_lastStatus = "Connection Failed";
        emit statusChanged(m_lastStatus);
    }
}

void TcpSessionWidget::checkStatus() {
    if (!m_active) return;

    bool connected = (m_receiver && m_receiver->isConnected());
    if (connected && m_lastStatus != "Connected") {
        m_lastStatus = "Connected";
        emit statusChanged(m_lastStatus);
    } else if (!connected && m_lastStatus == "Connected") {
        m_lastStatus = "Disconnected. Retrying...";
        emit statusChanged(m_lastStatus);
        if (!m_connectTimer->isActive()) {
            m_connectTimer->start(2000);
        }
    }
}
