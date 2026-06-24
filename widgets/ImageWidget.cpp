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
        
        // Small Box logic
        float small_draw_w = box_w;
        float small_draw_h = box_h;
        float small_draw_x = center_x - small_draw_w / 2.0f;
        float small_draw_y = center_y - small_draw_h / 2.0f;
        
        QColor smallBoxColor = boxColor;
        QColor smallLfBgColor = lfBgColor;
        if (!box.passedHostAppFilter) {
            smallBoxColor.setAlpha(180);
            smallLfBgColor.setAlpha(180);
        }
        
        QGraphicsRectItem *smallRectItem = new QGraphicsRectItem(small_draw_x, small_draw_y, small_draw_w, small_draw_h);
        QPen smallBorderPen(smallBoxColor, 2, penStyle);
        smallRectItem->setPen(smallBorderPen); 
        smallRectItem->setBrush(Qt::NoBrush);
        m_scene->addItem(smallRectItem);
        
        // Small Text (Just text, no background block, text color is the background color)
        QGraphicsTextItem *smallTextItem = new QGraphicsTextItem(displayText);
        smallTextItem->setDefaultTextColor(smallLfBgColor); // "翻转过来" -> Text is colored, no block
        QFont fSmall = smallTextItem->font();
        fSmall.setPixelSize(20); 
        fSmall.setBold(true);
        smallTextItem->setFont(fSmall);
        
        QRectF smallTextRect = smallTextItem->boundingRect();
        smallTextItem->setPos(small_draw_x + 5, small_draw_y - smallTextRect.height());
        m_scene->addItem(smallTextItem);

        // Big Box logic
        if (box.passedHostAppFilter) {
            float big_draw_w = box_w * 5.0f;
            float big_draw_h = box_h * 5.0f;
            float big_draw_x = center_x - big_draw_w / 2.0f;
            float big_draw_y = center_y - big_draw_h / 2.0f;

            QGraphicsRectItem *bigRectItem = new QGraphicsRectItem(big_draw_x, big_draw_y, big_draw_w, big_draw_h);
            QPen bigBorderPen(boxColor, 12, penStyle);
            bigRectItem->setPen(bigBorderPen); 
            bigRectItem->setBrush(QColor(boxColor.red(), boxColor.green(), boxColor.blue(), 50)); // 半透明幕布
            m_scene->addItem(bigRectItem);
            
            // Big Text (White text, colored background block)
            QGraphicsTextItem *bigTextItem = new QGraphicsTextItem(displayText);
            bigTextItem->setDefaultTextColor(Qt::white);
            QFont fBig = bigTextItem->font();
            fBig.setPixelSize(200); 
            fBig.setBold(true);
            bigTextItem->setFont(fBig);
            
            QRectF bigTextRect = bigTextItem->boundingRect();
            QGraphicsRectItem* bigTextBg = new QGraphicsRectItem(big_draw_x, big_draw_y - bigTextRect.height(), bigTextRect.width() + 10, bigTextRect.height());
            bigTextBg->setBrush(lfBgColor);
            bigTextBg->setPen(Qt::NoPen);
            m_scene->addItem(bigTextBg);
            
            bigTextItem->setPos(big_draw_x + 5, big_draw_y - bigTextRect.height());
            m_scene->addItem(bigTextItem);
        }
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

QImage ImageWidget::grabSceneImage() const
{
    // 按照用户要求，渲染整个场景到 1:1 的原分辨率 QImage
    QRectF sourceRect = m_scene->sceneRect();
    if (sourceRect.isEmpty()) return QImage();

    QSize targetSize = sourceRect.size().toSize();
    QImage image(targetSize, QImage::Format_RGB32);
    image.fill(Qt::black);
    
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    
    // 把 sceneRect 的内容绘制到 image 尺寸里
    m_scene->render(&painter, QRectF(0, 0, targetSize.width(), targetSize.height()), sourceRect);
    
    return image;
}

void ImageWidget::setHiddenDefects(const QStringList& defects)
{
    m_hiddenDefects = defects;
}
