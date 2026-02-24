#ifndef CHARTVIEW_H
#define CHARTVIEW_H

#include "ChartDelegate.h"
#include "ChartModel.h"

#include <QTableView>
#include <QSet>
#include <QTimer>

class ChartView : public QTableView
{
    Q_OBJECT
public:
    explicit ChartView(QWidget *parent = nullptr);

    void setChartModel(ChartModel *model);
    void setPixelsPerSample(float pps);
    void setRowHeight(int px);

    int  visibleSampleCount() const;

signals:
    void visibleSamplesChanged(int count, double pps);

public slots:
    void fitYToVisible();
    void toggleAutoFitY();
    void setAutoFitY(bool on);

    // Операции над выделением из ChartHeaderView
    void syncScale    (int sourceRow, const QSet<int> &rows);
    void overlayOnto  (int sourceRow, const QSet<int> &rows);
    void resetSelected(const QSet<int> &rows);

protected:
    void paintEvent       (QPaintEvent    *e) override;   // ← двухпроходная отрисовка
    void mouseMoveEvent   (QMouseEvent *e) override;
    void mousePressEvent  (QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent (QContextMenuEvent *e) override;
    void leaveEvent       (QEvent      *e) override;
    void wheelEvent       (QWheelEvent *e) override;
    void resizeEvent      (QResizeEvent *e) override;
    void keyPressEvent    (QKeyEvent   *e) override;

private slots:
    void onHScrollChanged(int value);
    void onCursorMoved(int sample);
    void onDataAppended(const QString &name, int row, int newTotalSamples);
    void flushPendingAppend();
    void flushZoom();
    void doAutoFitY();

private:
    int    viewXToSample(int viewX) const;
    int    viewYToRow   (int viewY) const;
    void   repaintCursorStrip(int oldSample, int newSample);
    void   syncColumnWidth();
    void   emitVisibleSamplesIfChanged();

    struct VisibleRange { double lo, hi; bool valid; };
    VisibleRange visibleRangeForRow(int row) const;

    ChartModel    *m_chartModel       = nullptr;
    ChartDelegate *m_delegate         = nullptr;

    int  m_prevCursorSample = -1;
    int  m_lastVisibleCount = -1;

    bool m_appendFlushPending = false;
    int  m_pendingOldSamples  = 0;
    int  m_pendingNewSamples  = 0;

    QTimer *m_zoomDebounceTimer = nullptr;
    float   m_pendingPps        = 0.f;
    int     m_pendingScroll     = -1;

    bool    m_dragging        = false;
    int     m_dragStartY      = 0;
    int     m_dragStartOffset = 0;
    QString m_dragSeriesName;

    bool    m_autoFitY        = false;
    QTimer *m_autoFitTimer    = nullptr;
};

#endif // CHARTVIEW_H
