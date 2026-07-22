#include "RoiSettingDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDebug>
#include "modules/CameraThread.h"
#include "MainWindow.h"

// ================== RoiImageView ==================

RoiImageView::RoiImageView(QWidget* parent) : QLabel(parent), m_isDrawing(false) {
    setMinimumSize(640, 480);
    setAlignment(Qt::AlignCenter);
    setStyleSheet("background-color: #1E1E1E; border: 1px solid #444;");
    setMouseTracking(true);
}

void RoiImageView::setImage(const QImage& img) {
    m_image = img;
    update();
}

QRect RoiImageView::getRoi() const {
    return m_roi;
}

void RoiImageView::setRoi(const QRect& r) {
    m_roi = r;
    update();
}

void RoiImageView::paintEvent(QPaintEvent* event) {
    QLabel::paintEvent(event);
    
    if (m_image.isNull()) return;

    QPainter painter(this);
    
    // 计算缩放比例，保持长宽比居中显示
    // 线阵双光相机数据是分时曝光交错的，因此视觉高度上需要压缩2倍，以匹配物理比例
    QSize virtualSize(m_image.width(), m_image.height() / 2);
    QSize scaledSize = virtualSize.scaled(size(), Qt::KeepAspectRatio);
    QRect imgRect(
        (width() - scaledSize.width()) / 2,
        (height() - scaledSize.height()) / 2,
        scaledSize.width(),
        scaledSize.height()
    );
    
    painter.drawImage(imgRect, m_image);

    // 绘制半透明遮罩
    QColor maskColor(0, 0, 0, 150);
    
    // 绘制 ROI 框
    QRect drawRect = m_isDrawing ? m_currentRect : m_roi;
    if (!drawRect.isEmpty()) {
        // 将原图坐标系下的 ROI 映射到显示坐标系
        double scaleX = (double)scaledSize.width() / m_image.width();
        double scaleY = (double)scaledSize.height() / m_image.height();
        
        QRect displayRoi(
            imgRect.x() + drawRect.x() * scaleX,
            imgRect.y() + drawRect.y() * scaleY,
            drawRect.width() * scaleX,
            drawRect.height() * scaleY
        );
        
        // 绘制 ROI 外部遮罩
        QRegion totalRegion(imgRect);
        QRegion roiRegion(displayRoi);
        painter.setClipRegion(totalRegion.subtracted(roiRegion));
        painter.fillRect(imgRect, maskColor);
        painter.setClipping(false);

        // 绘制红色边框
        painter.setPen(QPen(Qt::red, 2, Qt::SolidLine));
        painter.drawRect(displayRoi);
    } else {
        // 如果没有 ROI，全加遮罩
        painter.fillRect(imgRect, maskColor);
    }
}

void RoiImageView::mousePressEvent(QMouseEvent* event) {
    if (m_image.isNull() || event->button() != Qt::LeftButton) return;
    
    QSize virtualSize(m_image.width(), m_image.height() / 2);
    QSize scaledSize = virtualSize.scaled(size(), Qt::KeepAspectRatio);
    QRect imgRect(
        (width() - scaledSize.width()) / 2,
        (height() - scaledSize.height()) / 2,
        scaledSize.width(),
        scaledSize.height()
    );
    
    if (!imgRect.contains(event->pos())) return;
    
    m_isDrawing = true;
    m_startPoint = event->pos();
    
    // 映射回原图坐标
    double scaleX = (double)m_image.width() / scaledSize.width();
    double scaleY = (double)m_image.height() / scaledSize.height();
    
    int startX = (m_startPoint.x() - imgRect.x()) * scaleX;
    int startY = (m_startPoint.y() - imgRect.y()) * scaleY;
    
    m_currentRect = QRect(startX, startY, 0, 0);
}

void RoiImageView::mouseMoveEvent(QMouseEvent* event) {
    if (!m_isDrawing || m_image.isNull()) return;
    
    QSize virtualSize(m_image.width(), m_image.height() / 2);
    QSize scaledSize = virtualSize.scaled(size(), Qt::KeepAspectRatio);
    QRect imgRect(
        (width() - scaledSize.width()) / 2,
        (height() - scaledSize.height()) / 2,
        scaledSize.width(),
        scaledSize.height()
    );
    
    QPoint currentPoint = event->pos();
    
    // 限制在图像区域内
    currentPoint.setX(qBound(imgRect.left(), currentPoint.x(), imgRect.right()));
    currentPoint.setY(qBound(imgRect.top(), currentPoint.y(), imgRect.bottom()));
    
    double scaleX = (double)m_image.width() / scaledSize.width();
    double scaleY = (double)m_image.height() / scaledSize.height();
    
    int startX = (m_startPoint.x() - imgRect.x()) * scaleX;
    int startY = (m_startPoint.y() - imgRect.y()) * scaleY;
    
    int endX = (currentPoint.x() - imgRect.x()) * scaleX;
    int endY = (currentPoint.y() - imgRect.y()) * scaleY;
    
    m_currentRect = QRect(QPoint(startX, startY), QPoint(endX, endY)).normalized();
    update();
}

void RoiImageView::mouseReleaseEvent(QMouseEvent* event) {
    if (!m_isDrawing || event->button() != Qt::LeftButton) return;
    
    m_isDrawing = false;
    m_roi = m_currentRect;
    update();
    
    emit roiChanged(m_roi);
}

// ================== RoiSettingDialog ==================

RoiSettingDialog::RoiSettingDialog(int cameraId, QWidget* parent) 
    : QDialog(parent), m_cameraId(cameraId), m_maxWidth(16384), m_maxHeight(10000000), m_origImgW(16384), m_origImgH(10000000) 
{
    setWindowTitle(QString("相机 %1 ROI 实时设置").arg(cameraId));
    resize(1000, 700);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // 图片显示区
    m_imageView = new RoiImageView(this);
    mainLayout->addWidget(m_imageView, 1);
    
    // 控制区
    QHBoxLayout* controlLayout = new QHBoxLayout();
    
    QGroupBox* paramGroup = new QGroupBox("ROI 参数调整 (像素)", this);
    QHBoxLayout* paramLayout = new QHBoxLayout(paramGroup);
    
    m_spinX = new QSpinBox(this); m_spinX->setRange(-m_maxWidth, m_maxWidth); m_spinX->setKeyboardTracking(false);
    m_spinY = new QSpinBox(this); m_spinY->setRange(0, m_maxHeight); m_spinY->setKeyboardTracking(false);
    m_spinW = new QSpinBox(this); m_spinW->setRange(1, m_maxWidth); m_spinW->setKeyboardTracking(false);
    m_spinH = new QSpinBox(this); m_spinH->setRange(1, m_maxHeight); m_spinH->setKeyboardTracking(false);
    
    paramLayout->addWidget(new QLabel("Offset X(中心偏移):")); paramLayout->addWidget(m_spinX);
    paramLayout->addWidget(new QLabel("Offset Y(左上):")); paramLayout->addWidget(m_spinY);
    paramLayout->addWidget(new QLabel("Width:")); paramLayout->addWidget(m_spinW);
    paramLayout->addWidget(new QLabel("Height:")); paramLayout->addWidget(m_spinH);
    
    controlLayout->addWidget(paramGroup);
    
    m_btnRefresh = new QPushButton("抓取最新图像", this);
    m_btnApply = new QPushButton("确认", this);
    m_btnApply->setStyleSheet("background-color: #1A73E8; color: white; font-weight: bold;");
    
    controlLayout->addStretch();
    controlLayout->addWidget(m_btnRefresh);
    controlLayout->addWidget(m_btnApply);
    
    mainLayout->addLayout(controlLayout);
    
    connect(m_imageView, &RoiImageView::roiChanged, this, &RoiSettingDialog::onRoiViewChanged);
    connect(m_spinX, QOverload<int>::of(&QSpinBox::valueChanged), this, &RoiSettingDialog::onSpinBoxChanged);
    connect(m_spinY, QOverload<int>::of(&QSpinBox::valueChanged), this, &RoiSettingDialog::onSpinBoxChanged);
    connect(m_spinW, QOverload<int>::of(&QSpinBox::valueChanged), this, &RoiSettingDialog::onSpinBoxChanged);
    connect(m_spinH, QOverload<int>::of(&QSpinBox::valueChanged), this, &RoiSettingDialog::onSpinBoxChanged);
    
    connect(m_btnRefresh, &QPushButton::clicked, this, &RoiSettingDialog::onRefreshClicked);
    connect(m_btnApply, &QPushButton::clicked, this, &RoiSettingDialog::onApplyClicked);
}

void RoiSettingDialog::setMaxResolution(int maxWidth, int maxHeight) {
    m_maxWidth = maxWidth;
    m_maxHeight = maxHeight;
    m_spinX->setMaximum(m_maxWidth);
    m_spinY->setMaximum(m_maxHeight);
    m_spinW->setMaximum(m_maxWidth);
    m_spinH->setMaximum(m_maxHeight);
}

int RoiSettingDialog::roiX() const { 
    // m_spinX is Center Offset. Compute Top-Left X.
    int startX = (m_maxWidth / 2) + m_spinX->value() - (m_spinW->value() / 2);
    // return clamped to avoid out of bounds
    return qBound(0, startX, m_maxWidth - m_spinW->value());
}
int RoiSettingDialog::roiY() const { return m_spinY->value(); }
int RoiSettingDialog::roiWidth() const { return m_spinW->value(); }
int RoiSettingDialog::roiHeight() const { return m_spinH->value(); }

void RoiSettingDialog::setRoi(int x, int y, int w, int h) {
    m_spinX->blockSignals(true);
    m_spinY->blockSignals(true);
    m_spinW->blockSignals(true);
    m_spinH->blockSignals(true);
    
    m_spinW->setValue(w);
    m_spinH->setValue(h);
    
    // UI Offset X = ROI_Center_X - Sensor_Center_X
    int uiOffsetX = (x + w / 2) - (m_maxWidth / 2);
    m_spinX->setValue(uiOffsetX);
    m_spinY->setValue(y);
    
    m_spinX->blockSignals(false);
    m_spinY->blockSignals(false);
    m_spinW->blockSignals(false);
    m_spinH->blockSignals(false);
    
    m_imageView->setRoi(QRect(x, y, w, h));
}

void RoiSettingDialog::updateImage(std::shared_ptr<ImageFrame> frame) {
    if (!frame || frame->image.empty()) return;
    if (frame->cameraId != m_cameraId) return;
    
    m_origImgW = frame->image.cols;
    m_origImgH = frame->image.rows;
    // Removed setMaxResolution here so we don't constrain user to current frame height
    
    QImage qimg(frame->image.data, frame->image.cols, frame->image.rows, frame->image.step, QImage::Format_Grayscale8);
    m_imageView->setImage(qimg.copy()); // copy to detach
}

void RoiSettingDialog::onRefreshClicked() {
    // 触发相机抓图。为了实现简单，可以直接调用 MainWindow 中已有的 CameraThread 的软触发
    // 或者我们直接发一个信号出去，让外面帮忙采图
    // 这里我们可以找到 MainWindow 实例
    // 假设 MainWindow 有个全局可访问的方法或者通过 parent 找到
    // 但这里最安全的是发个信号，或者直接通知所有 CameraThread 软触发一下
    // 因为这只是个对话框，我们临时让第一个 CameraThread 软触发一下
    QWidget* p = parentWidget();
    while (p && !p->inherits("MainWindow")) {
        p = p->parentWidget();
    }
    if (p) {
        // 调用软触发
        QMetaObject::invokeMethod(p, "triggerCameraForRoi", Q_ARG(int, m_cameraId));
    }
}

void RoiSettingDialog::onApplyClicked() {
    accept();
}

void RoiSettingDialog::onRoiViewChanged(const QRect& r) {
    bool block = signalsBlocked();
    blockSignals(true);
    m_spinW->setValue(r.width());
    m_spinH->setValue(r.height());
    
    // UI Offset X = ROI_Center_X - Sensor_Center_X
    int uiOffsetX = (r.x() + r.width() / 2) - (m_maxWidth / 2);
    m_spinX->setValue(uiOffsetX);
    m_spinY->setValue(r.y());
    blockSignals(block);
}

void RoiSettingDialog::onSpinBoxChanged() {
    int startX = (m_maxWidth / 2) + m_spinX->value() - (m_spinW->value() / 2);
    startX = qBound(0, startX, m_maxWidth - m_spinW->value());
    QRect r(startX, m_spinY->value(), m_spinW->value(), m_spinH->value());
    m_imageView->setRoi(r);
}
