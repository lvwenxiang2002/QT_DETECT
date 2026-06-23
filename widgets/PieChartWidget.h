#ifndef PIECHARTWIDGET_H
#define PIECHARTWIDGET_H

#include <QWidget>
#include <QPainter>
#include <QPaintEvent>

class PieChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PieChartWidget(QWidget *parent = nullptr);

    void setStats(int ngCount, int okCount);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_ngCount;
    int m_okCount;
};

#endif // PIECHARTWIDGET_H
