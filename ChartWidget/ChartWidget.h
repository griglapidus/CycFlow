#pragma once

#include "ChartHeaderView.h"
#include "ChartModel.h"
#include "ChartView.h"

#include <QWidget>
#include <QLabel>
#include <QResizeEvent>
#include <QShowEvent>

class QAction;
class QToolBar;

class ChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ChartWidget(QWidget *parent = nullptr);

    ChartModel      *model()      { return m_model;      }
    ChartView       *view()       { return m_view;       }
    ChartHeaderView *headerView() { return m_headerView; }

protected:
    void resizeEvent(QResizeEvent *e) override;
    void showEvent  (QShowEvent   *e) override;

private slots:
    void onCursorMoved(int sample);

private:
    void updateToolbarLayout();

    ChartModel      *m_model      = nullptr;
    ChartView       *m_view       = nullptr;
    ChartHeaderView *m_headerView = nullptr;

    QToolBar *m_tb         = nullptr;
    QWidget  *m_tbSpacer   = nullptr;
    QLabel   *m_tsLabel    = nullptr;
    QLabel   *m_hintsLabel = nullptr;
};
