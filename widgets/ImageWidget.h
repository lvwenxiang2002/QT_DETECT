#ifndef IMAGEWIDGET_H
#define IMAGEWIDGET_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QWheelEvent>
#include <QMouseEvent>
#include <memory>
#include "../Common.h"

class ImageWidget : public QGraphicsView
{
    Q_OBJECT
public:
    explicit ImageWidget(QWidget *parent = nullptr);
    ~ImageWidget();

    // 更新图像和检测结果
    void updateImage(std::shared_ptr<ImageFrame> frame, const cv::Mat& imgBL, const cv::Mat& imgDF);

    void setHiddenDefects(const QStringList& defects);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QGraphicsScene *m_scene;
    QGraphicsPixmapItem *m_pixmapItemBL;
    QGraphicsPixmapItem *m_pixmapItemDF;
    QPoint m_lastMousePos;
    bool m_isPanning;
    qreal m_scaleFactor;
    QStringList m_hiddenDefects;
};

#endif // IMAGEWIDGET_H
