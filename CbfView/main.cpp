#include <QApplication>
#include <QThread>
#include <QTimer>
#include <cmath>
#include "ChartWidget.h"
#include "DataGenerator.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle("Fusion");

    qRegisterMetaType<QList<SeriesBatch>>("QList<SeriesBatch>");

    ChartWidget w;
    w.setWindowTitle("ChartWidget Demo");
    w.resize(1280, 640);

    ChartModel *model = w.model();

    model->addSeries("Voltage",  QColor(80,  200, 120), SampleType::Float32);
    model->addSeries("Current",  QColor(100, 160, 255), SampleType::Float32);
    model->addSeries("ADC ch0",  QColor(255, 140,  60), SampleType::Int16);
    model->addSeries("Pressure", QColor(220,  80, 180), SampleType::Float64);

    {
        QVector<float>   vf(2000), cf(2000);
        QVector<int16_t> adc(2000);
        QVector<double>  pres(2000);
        for (int i = 0; i < 2000; ++i) {
            vf[i]   = 12.f  + 2.5f * std::sin(i * 0.05f);
            cf[i]   =  3.2f + 0.8f * std::sin(i * 0.08f);
            adc[i]  = static_cast<int16_t>(std::sin(i * 0.02) * 5000 + 500);
            pres[i] = 101.3 + 1.2 * std::sin(i * 0.03);
        }
        model->appendBatch({
                            { "Voltage",  QVector<float>  (vf)   },
                            { "Current",  QVector<float>  (cf)   },
                            { "ADC ch0",  QVector<int16_t>(adc)  },
                            { "Pressure", QVector<double> (pres) },
                            });
    }

    auto *genThread = new QThread(&app);
    auto *gen       = new DataGenerator;
    gen->moveToThread(genThread);

    gen->setConfigs({
                     { "Voltage",  0.05f, 2.5f, 12.0f },
                     { "Current",  0.08f, 0.8f,  3.2f },
                     });
    gen->setBatchSize(10);
    gen->setIntervalMs(50);

    QObject::connect(gen, &DataGenerator::batchReady,
                     model, &ChartModel::appendBatch);

    QObject::connect(genThread, &QThread::started, gen, &DataGenerator::start);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [=]() {
        gen->stop();
        genThread->quit();
        genThread->wait(2000);
        delete gen;
    });

    genThread->start();

    static int tick = 2000;
    auto *timer = new QTimer(&app);
    QObject::connect(timer, &QTimer::timeout, [&]() {
        QVector<int16_t> adc(10);
        QVector<double>  pres(10);
        for (int i = 0; i < 10; ++i) {
            adc[i]  = static_cast<int16_t>(std::sin((tick + i) * 0.02) * 5000 + 500);
            pres[i] = 101.3 + 1.2 * std::sin((tick + i) * 0.03);
        }
        model->appendBatch({
                            { "ADC ch0",  QVector<int16_t>(adc)  },
                            { "Pressure", QVector<double> (pres) },
                            });
        tick += 10;
    });
    timer->start(50);

    w.show();
    return app.exec();
}
