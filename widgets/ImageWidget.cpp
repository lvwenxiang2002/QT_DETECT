#include "ImageWidget.h"
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QPen>
#include <QScrollBar>
#include <cmath>

ImageWidget::ImageWidget(QWidget *parent)
    : QGraphicsView(parent), m_isPanning(false)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    m_pixmapItemBL = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixmapItemBL);
    
    m_pixmapItemDF = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixmapItemDF);

    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::NoDrag); 
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

ImageWidget::~ImageWidget()
{
}

void ImageWidget::updateImage(std::shared_ptr<ImageFrame> frame, const cv::Mat& imgBL, const cv::Mat& imgDF)
{
    if (!frame) return;

    // 清除上一次的绘制框
    QList<QGraphicsItem*> items = m_scene->items();
    for (QGraphicsItem* item : items) {
        if (item != m_pixmapItemBL && item != m_pixmapItemDF) {
            m_scene->removeItem(item);
            delete item;
        }
    }

    if (!imgBL.empty()) {
        QImage qimgBL(imgBL.data, imgBL.cols, imgBL.rows, imgBL.step, QImage::Format_Grayscale8);
        m_pixmapItemBL->setPixmap(QPixmap::fromImage(qimgBL.copy()));
        m_pixmapItemBL->setPos(0, 0);
    }
    
    // Do not display imgDF in the UI, as requested by the user.
    if (m_pixmapItemDF) {
        m_pixmapItemDF->setVisible(false);
    }
    
    QRectF totalRect = m_pixmapItemBL->boundingRect();
    m_scene->setSceneRect(totalRect);
    
    // Fit the entire 16K image in the view so we don't just see the top-left black corner
    this->fitInView(totalRect, Qt::KeepAspectRatio);

    this->setBackgroundBrush(QColor("#7A7A7A"));

    // 缺陷类别映射字典 (更新为最新中英文对照表)
    QStringList defectNames = {
        QString::fromUtf8("崩边"),          // 0: broken
        QString::fromUtf8("划伤"),          // 1: scratch
        QString::fromUtf8("黑划伤"),        // 2: scratch_black
        QString::fromUtf8("凹凸点"),        // 3: pit
        QString::fromUtf8("不可擦拭脏污"),  // 4: squalidity
        QString::fromUtf8("点伤"),          // 5: spot_defect
        QString::fromUtf8("异色")           // 6: discoloration
    };

    // 绘制推理结果框 (最终所有框都绘制到背光图上)
    bool isNG = frame->results.size() > 0;
    for (const auto& box : frame->results) {
        QString label = (box.class_id >= 0 && box.class_id < defectNames.size()) 
                        ? defectNames[box.class_id] : "unknown";
        QString lfPrefix = (box.light_type == LightField::DarkField) ? "暗场-" : "背光-";
        QString fullLabel = lfPrefix + label;

        if (m_hiddenDefects.contains(fullLabel)) {
            continue; 
        }

        // 定义各缺陷类型的专属颜色
        static const QVector<QColor> defectColors = {
            QColor("#00FFFF"), // 0: 崩边 (Cyan)
            QColor("#FF00FF"), // 1: 划伤 (Magenta)
            QColor("#8A2BE2"), // 2: 黑划伤 (BlueViolet)
            QColor("#FFA500"), // 3: 凹凸点 (Orange)
            QColor("#8B4513"), // 4: 不可擦拭脏污 (SaddleBrown)
            QColor("#FF69B4"), // 5: 点伤 (HotPink)
            QColor("#32CD32")  // 6: 异色 (LimeGreen)
        };
        
        QColor boxColor = (box.class_id >= 0 && box.class_id < defectColors.size()) ? defectColors[box.class_id] : Qt::white;
        QColor lfBgColor = (box.light_type == LightField::DarkField) ? QColor("#D4AF37") : QColor("#DC143C"); // 暗场金，背光红
        Qt::PenStyle penStyle = (box.light_type == LightField::DarkField) ? Qt::DashLine : Qt::SolidLine;

        float box_w = box.x2 - box.x1;
        float box_h = box.y2 - box.y1;
        
        // 计算面积(mm²)和对角线(mm) —— 坐标已处于物理大图绝对空间 精度 0.039mm/px
        const float MM_PER_PX = 0.039f;
        float area_mm2 = box_w * box_h * MM_PER_PX * MM_PER_PX;
        float diag_mm  = std::sqrt(box_w * box_h * MM_PER_PX * MM_PER_PX); // Wait, wrong formula originally? Wait, I am fixing it!
        diag_mm = std::sqrt(box_w * box_w + box_h * box_h) * MM_PER_PX;

        // 组装标签文字: "黑点  81%  S:12  D:5"
        QString displayText = QString("%1  %2%  S:%3  D:%4")
            .arg(label)
            .arg((int)(box.confidence * 100))
            .arg(area_mm2, 0, 'f', 1)
            .arg(diag_mm,  0, 'f', 1);
        
        // 坐标偏移：所有框都画在同一张图上，不再偏移
        float center_x = box.x1 + box_w / 2.0f;
        float center_y = box.y1 + box_h / 2.0f;
        
        float draw_w = box_w;
        float draw_h = box_h;
        int pen_width = 4;
        int font_size = 40;
        
        if (box.passedHostAppFilter) {
            draw_w *= 5.0f;
            draw_h *= 5.0f;
            pen_width = 12; // 边框也加粗一点
            font_size = 200; // 字体放大五倍
        } else {
            // 没有通过上位机参数的，存在感低一点，全透明小框
            boxColor.setAlpha(180);
            lfBgColor.setAlpha(180);
            pen_width = 2;
            font_size = 20; // 缩小一半
        }

        float draw_x = center_x - draw_w / 2.0f;
        float draw_y = center_y - draw_h / 2.0f;

        QGraphicsRectItem *rectItem = new QGraphicsRectItem(draw_x, draw_y, draw_w, draw_h);
        QPen borderPen(boxColor, pen_width, penStyle);
        rectItem->setPen(borderPen); 
        
        if (box.passedHostAppFilter) {
            rectItem->setBrush(QColor(boxColor.red(), boxColor.green(), boxColor.blue(), 50)); 
        } else {
            rectItem->setBrush(Qt::NoBrush); // 全透明无幕布
        }
        
        m_scene->addItem(rectItem);
        
        QGraphicsTextItem *textItem = new QGraphicsTextItem(displayText);
        textItem->setDefaultTextColor(Qt::white);
        QFont f = textItem->font();
        f.setPixelSize(font_size); 
        f.setBold(true);
        textItem->setFont(f);
        
        QRectF textRect = textItem->boundingRect();
        QGraphicsRectItem* textBg = new QGraphicsRectItem(draw_x, draw_y - textRect.height(), textRect.width() + 10, textRect.height());
        textBg->setBrush(lfBgColor);
        textBg->setPen(Qt::NoPen);
        
        m_scene->addItem(textBg);
        
        textItem->setPos(draw_x + 5, draw_y - textRect.height());
        m_scene->addItem(textItem);
    }
}

void ImageWidget::wheelEvent(QWheelEvent *event)
{
    double scaleFactor = 1.15;
    if (event->angleDelta().y() > 0) {
        scale(scaleFactor, scaleFactor);
    } else {
        scale(1.0 / scaleFactor, 1.0 / scaleFactor);
    }
}

void ImageWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isPanning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
    QGraphicsView::mousePressEvent(event);
}

void ImageWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isPanning) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
    }
    QGraphicsView::mouseMoveEvent(event);
}

void ImageWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isPanning = false;
        setCursor(Qt::ArrowCursor);
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ImageWidget::setHiddenDefects(const QStringList& defects)
{
    m_hiddenDefects = defects;
}
