#ifndef CHARTHEADERVIEW_H
#define CHARTHEADERVIEW_H

#include "ChartModel.h"

#include <QWidget>
#include <QAbstractScrollArea>

class ChartHeaderView : public QWidget
{
    Q_OBJECT
public:
    explicit ChartHeaderView(ChartModel *model, QWidget *parent = nullptr);

    void syncVerticalScroll(QAbstractScrollArea *chartView);

protected:
    void paintEvent(QPaintEvent *e) override;

private slots:
    void onCursorMoved(int sample);
    void onVerticalScroll(int value);

private:
    void paintRow(QPainter *p, const QRect &r,
                  const ChartSeries &s, int cursor) const;

    ChartModel *m_model;
    int         m_scrollOffset = 0;   // вертикальный скролл в пикселях
};

#endif // CHARTHEADERVIEW_H
