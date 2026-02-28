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

    // Форматирование epoch-секунд в читаемую строку (используется также тулбаром)
    static QString formatTimestamp(double epochSec);
    // Однострочная версия для тулбара (без \n)
    static QString formatTimestampLine(double epochSec);

    // Вызывается из ChartWidget, чтобы header знал текущее состояние авто-подстройки
    void setAutoFitY(bool on);

signals:
    // Операции над выделением — ChartView подключается и выполняет
    void syncScaleRequested    (int sourceRow, QSet<int> rows);
    void overlayRequested      (int sourceRow, QSet<int> rows);
    void resetSelectedRequested(QSet<int> rows);

    // View-level операции, перенесённые из контекстного меню ChartView
    void fitYToVisibleRequested();
    void autoFitYToggleRequested();

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

    static constexpr int kResizeZone       = 5;
    static constexpr int kHeaderResizeZone = 6;
    static constexpr int kHeaderMinWidth   = 80;
    static constexpr int kHeaderMaxWidth   = 500;
    int rowAtResizeHandle(int y) const;
    int rowAt(int y) const;        // строка под указателем (не resize-зона)
    int rowTop(int row) const;

    ChartModel *m_model;
    int         m_scrollOffset = 0;

    // Resize строки по вертикали
    bool m_resizing     = false;
    int  m_resizeRow    = -1;
    int  m_resizePressY = 0;
    int  m_resizeStartH = 0;

    // Resize ширины заголовка (drag правого края)
    bool m_headerResizing      = false;
    int  m_headerResizePressX  = 0;
    int  m_headerResizeStartW  = 0;

    // Selection
    QSet<int> m_selectedRows;
    int       m_lastClickedRow = -1;   // для Shift-клика (на будущее)

    // Зеркало состояния ChartView::m_autoFitY (обновляется через setAutoFitY)
    bool m_autoFitY = false;
};

#endif // CHARTHEADERVIEW_H
