#pragma once

#include "ChartHeaderView.h"
#include "ChartModel.h"
#include "ChartView.h"

#include <QWidget>

class QAction;

class ChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ChartWidget(QWidget *parent = nullptr);

    ChartModel      *model()      { return m_model;      }
    ChartView       *view()       { return m_view;       }
    ChartHeaderView *headerView() { return m_headerView; }

private:
    ChartModel      *m_model      = nullptr;
    ChartView       *m_view       = nullptr;
    ChartHeaderView *m_headerView = nullptr;
    QAction         *m_actAutoFit = nullptr;
};
