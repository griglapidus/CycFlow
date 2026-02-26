#ifndef CHARTHEADERVIEW_H
#define CHARTHEADERVIEW_H

#include "ChartModel.h"

#include <QWidget>
#include <QString>
#include <QAbstractScrollArea>
#include <QSet>

class ChartHeaderView : public QWidget
{
    Q_OBJECT
public:
    explicit ChartHeaderView(ChartModel *model, QWidget *parent = nullptr);

    void syncVerticalScroll(QAbstractScrollArea *chartView);

    // Текущее выделение
    QSet<int> selectedRows() const { return m_selectedRows; }

signals:
    // Операции над выделением — ChartView подключается и выполняет
    void syncScaleRequested  (int sourceRow, QSet<int> rows);
    void overlayRequested    (int sourceRow, QSet<int> rows);
    void resetSelectedRequested(QSet<int> rows);

protected:
    void paintEvent        (QPaintEvent   *) override;
    void mouseMoveEvent    (QMouseEvent   *) override;
    void mousePressEvent   (QMouseEvent   *) override;
    void mouseReleaseEvent (QMouseEvent   *) override;
    void contextMenuEvent  (QContextMenuEvent *) override;
    void leaveEvent        (QEvent        *) override;

private slots:
    void onCursorMoved    (int sample);
    void onVerticalScroll (int value);
    void onLayoutChanged  ();

private:
    void paintRow(QPainter *p, const QRect &r,
                  const ChartSeries &s, int cursor, bool selected) const;

    static constexpr int kResizeZone = 5;
    static QString formatTimestamp(double epochSec);
    int rowAtResizeHandle(int y) const;
    int rowAt(int y) const;        // строка под указателем (не resize-зона)
    int rowTop(int row) const;

    ChartModel *m_model;
    int         m_scrollOffset = 0;

    // Resize drag
    bool m_resizing     = false;
    int  m_resizeRow    = -1;
    int  m_resizePressY = 0;
    int  m_resizeStartH = 0;

    // Selection
    QSet<int> m_selectedRows;
    int       m_lastClickedRow = -1;   // для Shift-клика (на будущее)
};

#endif // CHARTHEADERVIEW_H
