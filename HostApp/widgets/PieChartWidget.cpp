#include "PieChartWidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QDebug>

PieChartWidget::PieChartWidget(QWidget *parent)
    : QWidget(parent), m_ngCount(0), m_okCount(0)
{
    setMinimumSize(250, 250);
}

void PieChartWidget::setStats(int ngCount, int okCount)
{
    m_ngCount = ngCount;
    m_okCount = okCount;
    update();
}

void PieChartWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int total = m_ngCount + m_okCount;
    
    int side = qMin(width(), height()) - 40;
    QRectF rect(width() / 2.0 - side / 2.0, height() / 2.0 - side / 2.0, side, side);

    if (total == 0) {
        painter.setBrush(QColor("#E0E0E0")); // Light gray for empty state
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(rect);
        return;
    }

    // Define colors as in the screenshot
    QColor colorNG("#E53935"); // Red
    QColor colorOK("#1E56FF"); // Blue

    // Calculate angles (in 1/16ths of a degree)
    int spanNG = (m_ngCount * 360 * 16) / total;
    int spanOK = 360 * 16 - spanNG; 

    // Draw NG slice
    painter.setBrush(colorNG);
    painter.setPen(Qt::NoPen);
    painter.drawPie(rect, 90 * 16, spanNG);

    // Draw OK slice
    painter.setBrush(colorOK);
    painter.drawPie(rect, 90 * 16 + spanNG, spanOK);

    // Draw Text Labels inside the slices
    painter.setPen(Qt::white);
    QFont f = font();
    f.setPointSize(10);
    painter.setFont(f);

    float ratioNG = (m_ngCount * 100.0f) / total;
    float ratioOK = 100.0f - ratioNG;

    // A simple way to put text in the approximate center of each slice
    // Text for NG
    if (m_ngCount > 0) {
        float angleNG = (90 + (spanNG / 16.0f) / 2.0f) * 3.14159f / 180.0f;
        int tx = rect.center().x() + (rect.width()/3) * cos(angleNG);
        int ty = rect.center().y() - (rect.height()/3) * sin(angleNG);
        painter.drawText(QRect(tx - 70, ty - 20, 140, 40), Qt::AlignCenter, QString("NG:%1%, %2片").arg(ratioNG, 0, 'f', 0).arg(m_ngCount));
    }

    // Text for OK
    if (m_okCount > 0) {
        float angleOK = (90 + (spanNG / 16.0f) + (spanOK / 16.0f) / 2.0f) * 3.14159f / 180.0f;
        int tx = rect.center().x() + (rect.width()/3) * cos(angleOK);
        int ty = rect.center().y() - (rect.height()/3) * sin(angleOK);
        painter.drawText(QRect(tx - 70, ty - 20, 140, 40), Qt::AlignCenter, QString("OK:%1%, %2片").arg(ratioOK, 0, 'f', 0).arg(m_okCount));
    }
}
