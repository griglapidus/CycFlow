#ifndef CHARTVIEW_H
#define CHARTVIEW_H

#include "ChartDelegate.h"
#include "ChartModel.h"

#include <QTableView>

class ChartView : public QTableView
{
    Q_OBJECT
public:
    explicit ChartView(QWidget *parent = nullptr);

    void setChartModel(ChartModel *model);
    void setPixelsPerSample(float pps);
    void setRowHeight(int px);
    void fitHeightToVisible();

    int visibleSampleCount() const;

signals:
    void visibleSamplesChanged(int count, double pps);

protected:
    void mouseMoveEvent   (QMouseEvent *e) override;
    void mousePressEvent  (QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void leaveEvent       (QEvent      *e) override;
    void wheelEvent       (QWheelEvent *e) override;
    void resizeEvent      (QResizeEvent *e) override;
    void keyPressEvent    (QKeyEvent   *e) override;

private slots:
    void onHScrollChanged(int value);
    void onCursorMoved(int sample);
    void onDataAppended(int row, int newTotalSamples);
    void flushPendingAppend();
    void flushZoom();

private:
    int  viewXToSample(int viewX) const;
    void repaintCursorStrip(int oldSample, int newSample);
    void syncColumnWidth();
    void emitVisibleSamplesIfChanged();

    ChartModel    *m_chartModel       = nullptr;
    ChartDelegate *m_delegate         = nullptr;
    int            m_prevCursorSample = -1;
    int            m_lastVisibleCount = -1;

    bool m_appendFlushPending = false;
    int  m_pendingOldSamples  = 0;
    int  m_pendingNewSamples  = 0;

    QTimer *m_zoomDebounceTimer = nullptr;
    float   m_pendingPps        = 0.f;
    int     m_pendingScroll     = -1;
};

#endif // CHARTVIEW_H
