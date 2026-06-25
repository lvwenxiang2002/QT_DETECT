#ifndef ROISETTINGDIALOG_H
#define ROISETTINGDIALOG_H

#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QMouseEvent>
#include <QPainter>
#include <opencv2/opencv.hpp>
#include <memory>
#include "../Common.h"

class RoiImageView : public QLabel {
    Q_OBJECT
public:
    explicit RoiImageView(QWidget* parent = nullptr);

    void setImage(const QImage& img);
    QRect getRoi() const;
    void setRoi(const QRect& r);

signals:
    void roiChanged(const QRect& newRoi);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QImage m_image;
    QRect m_roi;
    bool m_isDrawing;
    QPoint m_startPoint;
    
    // 鼠标拖拽框的逻辑
    QRect m_currentRect;
};

class RoiSettingDialog : public QDialog {
    Q_OBJECT
public:
    explicit RoiSettingDialog(int cameraId = 0, QWidget* parent = nullptr);

    // 传入原始宽高的限制
    void setMaxResolution(int maxWidth, int maxHeight);

    int roiX() const;
    int roiY() const;
    int roiWidth() const;
    int roiHeight() const;
    float exposureTime() const;

    void setRoi(int x, int y, int w, int h);
    void setExposure(float expTime);

public slots:
    void updateImage(std::shared_ptr<ImageFrame> frame);

private slots:
    void onRefreshClicked();
    void onApplyClicked();
    void onRoiViewChanged(const QRect& r);
    void onSpinBoxChanged();

private:
    int m_cameraId;
    RoiImageView* m_imageView;
    QSpinBox* m_spinX;
    QSpinBox* m_spinY;
    QSpinBox* m_spinW;
    QSpinBox* m_spinH;
    QDoubleSpinBox* m_spinExposure;
    QPushButton* m_btnRefresh;
    QPushButton* m_btnApply;

    int m_maxWidth;
    int m_maxHeight;

    // 当前抓取到的原图尺寸
    int m_origImgW;
    int m_origImgH;
};

#endif // ROISETTINGDIALOG_H
