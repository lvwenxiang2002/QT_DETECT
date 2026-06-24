#include "MainWindow.h"
#include "modules/RecipeManager.h"
#include <QDateTime>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QScrollArea>
#include <QVariant>
#include <QButtonGroup>
#include <QInputDialog>
#include <QMessageBox>
#include <QtConcurrent>
#include <QDir>
#include <opencv2/opencv.hpp>
#include "modules/ConfigManager.h"
#include "widgets/Dialogs.h"

MainWindow::MainWindow(QWidget *parent)
: QMainWindow(parent), m_totalCount(0), m_ngCount(0), m_currentFrameIndex(-1),
  m_imageWidgetLeft(nullptr), m_imageWidgetRight(nullptr), m_pieChart(nullptr)
{
    setupLightTheme();
    setupUi();

    // Init dual inference and camera threads
    for (int i = 0; i < 2; ++i) {
        InferThread* inferTh = new InferThread(this);
        connect(inferTh, &InferThread::inferenceCompleted, this, &MainWindow::onImageProcessed);
        inferTh->start();
        m_inferThreads.push_back(inferTh);

        CameraThread* camThread = new CameraThread(i, this);
        connect(camThread, &CameraThread::imageCaptured, inferTh, &InferThread::pushImage, Qt::DirectConnection);
        m_cameraThreads.push_back(camThread);
    }
    
    updateDefectTags();
}

MainWindow::~MainWindow()
{
    onStopClicked();
    for (auto inferTh : m_inferThreads) {
        inferTh->stop();
    }
}

void MainWindow::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    setWindowTitle("聚玻Vision·AI智能检测系统");
    resize(1920, 1080);
    setMinimumSize(1280, 720);

    QVBoxLayout *mainVLayout = new QVBoxLayout(centralWidget);
    mainVLayout->setContentsMargins(0, 0, 0, 0);
    mainVLayout->setSpacing(0);

    // Top Header Bar
    QFrame *topBar = new QFrame(this);
    topBar->setStyleSheet("background-color: #EBF2FF; border-bottom: 1px solid #D0D0D0;");
    topBar->setFixedHeight(35);
    QHBoxLayout *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(20, 0, 20, 0);
    
    QLabel *logoTitle = new QLabel(" 聚玻Vision·AI智能检测系统", this);
    logoTitle->setStyleSheet("font-size: 16px; font-weight: bold; color: #333333;");
    topLayout->addWidget(logoTitle);
    topLayout->addStretch();
    mainVLayout->addWidget(topBar);

    // Menu Bar
    QFrame *menuBar = new QFrame(this);
    menuBar->setStyleSheet("background-color: #FFFFFF; border-bottom: 1px solid #E0E0E0;");
    menuBar->setFixedHeight(40);
    QHBoxLayout *menuLayout = new QHBoxLayout(menuBar);
    menuLayout->setContentsMargins(20, 0, 20, 0);
    menuLayout->setSpacing(30);
    
    QString menuStyle = "QPushButton { font-size: 16px; color: #555555; font-weight: bold; border: none; background: transparent; } "
                        "QPushButton:hover { color: #1A73E8; text-decoration: underline; }";
    m_btnConfig = new QPushButton("系统配置", this); m_btnConfig->setStyleSheet(menuStyle); m_btnConfig->setCursor(Qt::PointingHandCursor);
    m_btnDetect = new QPushButton("检测配置", this); m_btnDetect->setStyleSheet(menuStyle); m_btnDetect->setCursor(Qt::PointingHandCursor);
    m_btnLog = new QPushButton("日志", this); m_btnLog->setStyleSheet(menuStyle); m_btnLog->setCursor(Qt::PointingHandCursor);
    
    connect(m_btnConfig, &QPushButton::clicked, this, &MainWindow::onMenuConfigClicked);
    connect(m_btnDetect, &QPushButton::clicked, this, &MainWindow::onMenuDetectClicked);
    connect(m_btnLog, &QPushButton::clicked, this, &MainWindow::onMenuLogClicked);

    menuLayout->addWidget(m_btnConfig);
    menuLayout->addWidget(m_btnDetect);
    menuLayout->addWidget(m_btnLog);
    menuLayout->addStretch();
    mainVLayout->addWidget(menuBar);

    // Body Layout
    QHBoxLayout *bodyLayout = new QHBoxLayout();
    bodyLayout->setContentsMargins(10, 10, 10, 10);
    bodyLayout->setSpacing(10);
    
    // LEFT PANEL
    QVBoxLayout *leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(10);
    
    // Status legend & Defect Buttons
    QHBoxLayout *statusLayout = new QHBoxLayout();
    
    // Initialize RecipeManager
    RecipeManager::instance().load("recipes.json");
    
    m_statusTitle = new QLabel(QString("● 当前检测 - %1").arg(RecipeManager::instance().getCurrentRecipeName()), this);
    m_statusTitle->setStyleSheet("font-size: 16px; font-weight: bold;");
    statusLayout->addWidget(m_statusTitle);
    statusLayout->addStretch();
    
    QLabel *legend = new QLabel("<font color='#777777'>■</font> <font color='#777777'>未开启</font> &nbsp;&nbsp; "
                                "<font color='#00C853'>■</font> <font color='#777777'>开启</font> &nbsp;&nbsp; "
                                "<font color='#E53935'>■</font> <font color='#777777'>开启并报警</font>", this);
    legend->setStyleSheet("font-size: 12px;");
    statusLayout->addWidget(legend);
    leftLayout->addLayout(statusLayout);
    
    // Defect tags
    QScrollArea *tagsScroll = new QScrollArea(this);
    tagsScroll->setWidgetResizable(true);
    tagsScroll->setFixedHeight(65);
    tagsScroll->setFrameShape(QFrame::NoFrame);
    tagsScroll->setStyleSheet(
        "QScrollArea { border: 1px solid #E0E0E0; background-color: #FAFAFA; border-radius: 4px; margin-bottom: 5px; }"
        "QScrollBar:horizontal {"
        "    border: none; background: #F0F0F0; height: 10px; border-radius: 5px; margin: 0px 0px 0px 0px;"
        "}"
        "QScrollBar::handle:horizontal {"
        "    background: #C0C0C0; min-width: 20px; border-radius: 5px;"
        "}"
        "QScrollBar::handle:horizontal:hover { background: #A0A0A0; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
    );
    
    QWidget *tagsWidget = new QWidget();
    QHBoxLayout *tagsLayout = new QHBoxLayout(tagsWidget);
    tagsLayout->setContentsMargins(10, 0, 10, 0);
    tagsLayout->setSpacing(10);
    
    QStringList baseDefects = {"崩边", "划伤", "黑划伤", "凹凸点", "不可擦拭脏污", "点伤", "异色"};
    QStringList tags;
    for (const QString& def : baseDefects) {
        tags << QString("暗场-%1").arg(def);
    }
    for (const QString& def : baseDefects) {
        tags << QString("背光-%1").arg(def);
    }
    
    for (int i = 0; i < tags.size(); ++i) {
        QPushButton *t = new QPushButton(tags[i], tagsWidget);
        t->setFixedSize(110, 30);
        t->setCursor(Qt::PointingHandCursor);
        
        t->setProperty("defectState", 1);
        m_tagButtons[tags[i]] = t;
        
        connect(t, &QPushButton::clicked, this, &MainWindow::onDefectTagClicked);
        tagsLayout->addWidget(t);
    }
    
    tagsScroll->setWidget(tagsWidget);
    leftLayout->addWidget(tagsScroll);
    
    QLabel *lblResultTitle = new QLabel("▍ 表面检测结果", this);
    lblResultTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #333333; margin-top: 5px;");
    leftLayout->addWidget(lblResultTitle);
    
    // Main Images (Dual view)
    QHBoxLayout *imagesLayout = new QHBoxLayout();
    imagesLayout->setSpacing(0); // 取消中间的间隙
    
    m_imageWidgetLeft = new ImageWidget(this);
    m_imageWidgetLeft->setStyleSheet("border: 1px solid #D0D0D0; border-right: none; background-color: #7A7A7A;");
    m_imageWidgetLeft->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    imagesLayout->addWidget(m_imageWidgetLeft, 1);

    m_imageWidgetRight = new ImageWidget(this);
    m_imageWidgetRight->setStyleSheet("border: 1px solid #D0D0D0; border-left: none; background-color: #7A7A7A;");
    m_imageWidgetRight->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    imagesLayout->addWidget(m_imageWidgetRight, 1);

    leftLayout->addLayout(imagesLayout, 1);
    
    bodyLayout->addLayout(leftLayout, 4);
    
    // RIGHT PANEL
    QFrame *rightPanel = new QFrame(this);
    rightPanel->setFixedWidth(260);
    rightPanel->setStyleSheet("background-color: #FFFFFF; border: 1px solid #E0E0E0;");
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(15, 20, 15, 20);
    rightLayout->setSpacing(20);
    
    m_btnStart = new QPushButton("开始检测", this);
    m_btnStart->setFixedHeight(40);
    m_btnStart->setCursor(Qt::PointingHandCursor);
    m_btnStart->setStyleSheet(
        "QPushButton { background-color: #1A73E8; color: white; border-radius: 4px; font-size: 16px; font-weight: bold; } "
        "QPushButton:hover { background-color: #2B7DE9; } "
        "QPushButton:pressed { background-color: #1557B0; } "
        "QPushButton:disabled { background-color: #A0C3FF; color: #F0F0F0; }");
    connect(m_btnStart, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    
    m_btnStop = new QPushButton("关闭检测", this);
    m_btnStop->setFixedHeight(40);
    m_btnStop->setEnabled(false);
    m_btnStop->setCursor(Qt::PointingHandCursor);
    m_btnStop->setStyleSheet(
        "QPushButton { background-color: white; color: #1A73E8; border: 1px solid #1A73E8; border-radius: 4px; font-size: 16px; font-weight: bold; } "
        "QPushButton:hover { background-color: #F0F4FF; } "
        "QPushButton:pressed { background-color: #E0E9FF; } "
        "QPushButton:disabled { color: #A0C3FF; border: 1px solid #A0C3FF; background-color: white; }");
    connect(m_btnStop, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    
    rightLayout->addWidget(m_btnStart);
    rightLayout->addWidget(m_btnStop);
    
    rightLayout->addSpacing(30);
    
    m_lblPieceCount = new QLabel("第0片", this);
    m_lblPieceCount->setAlignment(Qt::AlignCenter);
    m_lblPieceCount->setStyleSheet("font-size: 36px; color: #333333; border: none;");
    rightLayout->addWidget(m_lblPieceCount);
    
    m_lblStatus = new QLabel("--", this);
    m_lblStatus->setAlignment(Qt::AlignCenter);
    m_lblStatus->setStyleSheet("font-size: 80px; font-weight: bold; color: #CCCCCC; border: none;");
    rightLayout->addWidget(m_lblStatus);
    
    rightLayout->addSpacing(30);
    
    // Tabs
    QHBoxLayout *tabsLayout = new QHBoxLayout();
    tabsLayout->setSpacing(5);
    QButtonGroup *tabGroup = new QButtonGroup(this);
    QStringList tabNames = {"今日", "三日内", "本周", "本月"};
    for (int i=0; i<4; ++i) {
        QPushButton *tab = new QPushButton(tabNames[i], this);
        tab->setCheckable(true);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setStyleSheet("QPushButton:checked { color: #1A73E8; font-size: 14px; font-weight: bold; border: none; background: transparent; } "
                           "QPushButton:!checked { color: #777777; font-size: 14px; border: none; background: transparent; }");
        if (i == 0) tab->setChecked(true);
        tabGroup->addButton(tab, i);
        tabsLayout->addWidget(tab);
    }
    connect(tabGroup, QOverload<int>::of(&QButtonGroup::idClicked), this, &MainWindow::onTimeTabClicked);
    rightLayout->addLayout(tabsLayout);
    
    QFrame *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #E0E0E0;");
    rightLayout->addWidget(line);
    
    QLabel *lblTotalText = new QLabel("检测总数", this);
    lblTotalText->setAlignment(Qt::AlignCenter);
    lblTotalText->setStyleSheet("font-size: 14px; color: #555555; border: none;");
    rightLayout->addWidget(lblTotalText);
    
    m_lblTotal = new QLabel("0片", this);
    m_lblTotal->setAlignment(Qt::AlignCenter);
    m_lblTotal->setStyleSheet("font-size: 28px; font-weight: bold; color: #1A73E8; border: none;");
    rightLayout->addWidget(m_lblTotal);
    
    m_pieChart = new PieChartWidget(this);
    rightLayout->addWidget(m_pieChart, 1);
    
    bodyLayout->addWidget(rightPanel);
    mainVLayout->addLayout(bodyLayout, 1);
}

void MainWindow::setupLightTheme()
{
    this->setStyleSheet("QMainWindow { background-color: #F5F5F5; }");
}

void MainWindow::onStartClicked()
{
    QList<QPushButton*> allBtns = this->findChildren<QPushButton*>();
    for (QPushButton* btn : allBtns) {
        if (btn != m_btnStop) {
            btn->setEnabled(false);
        }
    }
    m_btnStop->setEnabled(true);
    
    // 重置所有计数和双相机对齐追踪变量
    m_totalCount = 0;
    m_ngCount = 0;
    m_currentFrameIndex = -1;
    m_receivedCamerasPerFrame.clear();
    m_ngFrameIndices.clear();
    m_lblPieceCount->setText("第0片");
    m_lblStatus->setText("--");
    m_lblStatus->setStyleSheet("font-size: 80px; font-weight: bold; color: #CCCCCC; border: none;");
    m_lblTotal->setText("0片");
    m_pieChart->setStats(0, 0);
    
    for (auto cam : m_cameraThreads) {
        cam->startCapture();
    }
}

void MainWindow::onStopClicked()
{
    QList<QPushButton*> allBtns = this->findChildren<QPushButton*>();
    for (QPushButton* btn : allBtns) {
        btn->setEnabled(true);
    }
    m_btnStop->setEnabled(false);
    
    for (auto cam : m_cameraThreads) {
        cam->stopCapture();
    }
}

void MainWindow::onImageProcessed(std::shared_ptr<ImageFrame> frame)
{
    if (!frame) return;

    int src_w = frame->image.cols;
    int src_h = frame->image.rows;
    int split_h = src_h / 2;
    
    cv::Mat imgBL(split_h, src_w, frame->image.type());
    cv::Mat imgDF(split_h, src_w, frame->image.type());
    
    // De-interleave image
    if (split_h > 0 && src_h % 2 == 0) {
        for (int r = 0; r < split_h; ++r) {
            frame->image.row(r * 2).copyTo(imgBL.row(r));
            frame->image.row(r * 2 + 1).copyTo(imgDF.row(r));
        }
    } else {
        // Fallback for non-interleaved images
        imgBL = frame->image.clone();
        imgDF = frame->image.clone();
    }
    
    if (frame->cameraId == 0) {
        m_imageWidgetLeft->updateImage(frame, imgBL, imgDF);
    } else {
        m_imageWidgetRight->updateImage(frame, imgBL, imgDF);
    }

    bool isNG = false;
    QStringList defectNames = {"崩边", "划伤", "黑划伤", "凹凸点", "不可擦拭脏污", "点伤", "异色"};
    for (const auto& res : frame->results) {
        QString label = (res.class_id >= 0 && res.class_id < defectNames.size()) ? defectNames[res.class_id] : "unknown";
        QString lfPrefix = (res.light_type == LightField::DarkField) ? "暗场-" : "背光-";
        QString fullLabel = lfPrefix + label;
        if (res.passedHostAppFilter && !m_hiddenDefects.contains(fullLabel)) {
            isNG = true;
            break;
        }
    }
    if (ConfigManager::instance().storage.saveStrategy == 0 ||
        (ConfigManager::instance().storage.saveStrategy == 1 && isNG)) 
    {
        // 强制使用 C 盘进行 NVMe 固态硬盘极致 IO 测试
        QString rootPath = "C:/BLL_test_save"; 
        QString dateStr = QDateTime::currentDateTime().toString("yyyy-MM-dd");
        QString timestamp = QDateTime::currentDateTime().toString("HHmmss_zzz");
        QString camStr = QString("Camera_%1").arg(frame->cameraId);
        // 专门用于 CPU 图像压缩的线程池 (由于压缩耗时，且多核可并行)
        static QThreadPool compressPool;
        // 针对 NVMe 固态硬盘，开启多线程并发写入以榨干 PCIe 带宽
        static QThreadPool diskPool;
        static bool poolInit = false;
        if (!poolInit) {
            compressPool.setMaxThreadCount(6);
            diskPool.setMaxThreadCount(6); // NVMe 固态硬盘并发写入极快
            poolInit = true;
        }

        static std::atomic<int> pendingSaves{0};
        pendingSaves++;

        // 1. 将耗时的压缩任务扔给 compressPool
        QtConcurrent::run(&compressPool, [frame, rootPath, dateStr, timestamp, isNG, camStr, imgBL, imgDF]() {
            auto saveField = [&](const cv::Mat& img, LightField lf, const QString& lfStr) {
                if (img.empty()) return;
                    
                    QString oriDir = isNG ? QString("%1/%2/NG_ori/%3/%4").arg(rootPath).arg(dateStr).arg(camStr).arg(lfStr)
                                          : QString("%1/%2/OK_ori/%3/%4").arg(rootPath).arg(dateStr).arg(camStr).arg(lfStr);
                    QString resultDir = QString("%1/%2/NG_result/%3/%4").arg(rootPath).arg(dateStr).arg(camStr).arg(lfStr);
                    
                    QDir().mkpath(oriDir);
                    QString filenameBase = QString("%1_%2_%3_%4").arg(timestamp).arg(camStr).arg(lfStr).arg(isNG ? "NG" : "OK");
                    
                    // 1. Save original image as PNG with low compression to avoid OOM
                    QString oriFile = QString("%1/%2.png").arg(oriDir).arg(filenameBase);
                    std::vector<int> compression_params;
                    compression_params.push_back(cv::IMWRITE_PNG_COMPRESSION);
                    compression_params.push_back(1); // Low compression = much faster
                    
                    // 先在内存中将 Mat 压缩为 PNG 字节流 (完全吃 CPU，不沾硬盘 IO)
                    std::vector<uchar> oriBuffer;
                    cv::imencode(".png", img, oriBuffer, compression_params);
                    
                    // 将压缩好的字节流丢给单线程的 diskPool 进行极致顺序写入
                    QtConcurrent::run(&diskPool, [oriFile, buffer = std::move(oriBuffer)]() {
                        QFile file(oriFile);
                        if (file.open(QIODevice::WriteOnly)) {
                            file.write((const char*)buffer.data(), buffer.size());
                            file.close();
                        }
                    });
                    
                    // NG 绘图被移到了下方统一处理
                };
                saveField(imgBL, LightField::Backlight, "Backlight");
                saveField(imgDF, LightField::DarkField, "Darkfield");

                // 2. 统一绘制和保存 NG_result (缩放后一次性绘制所有光场的缺陷，彻底解决大图转换 OOM 问题)
                if (isNG) {
                    QString resultDir = QString("%1/%2/NG_result/%3").arg(rootPath).arg(dateStr).arg(camStr);
                    QDir().mkpath(resultDir);
                    QString filenameBase = QString("%1_%2_Result_NG").arg(timestamp).arg(camStr);
                    QString resultFile = QString("%1/%2.jpg").arg(resultDir).arg(filenameBase);
                    
                    // 将大图缩放到 1/5 进行展示和保存，内存占用下降 25 倍，渲染飞快
                    const float scale = 0.2f;
                    cv::Mat resultImg;
                    cv::resize(frame->image, resultImg, cv::Size(), scale, scale, cv::INTER_LINEAR);
                    if (resultImg.channels() == 1) cv::cvtColor(resultImg, resultImg, cv::COLOR_GRAY2BGR);
                    
                    std::vector<std::string> defectNames = {
                        "broken", "scratch", "scratch_black", "pit", 
                        "squalidity", "spot_defect", "discoloration"
                    };
                    
                    for (const auto& box : frame->results) {
                        cv::Scalar boxColor = (box.light_type == LightField::DarkField) ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 255);
                        cv::Scalar lfBgColor = (box.light_type == LightField::DarkField) ? cv::Scalar(0, 0, 255) : cv::Scalar(255, 100, 0); // BGR
                        
                        float center_x = (box.x1 + (box.x2 - box.x1) / 2.0f) * scale;
                        float center_y = (box.y1 + (box.y2 - box.y1) / 2.0f) * scale;
                        float small_w = (box.x2 - box.x1) * scale;
                        float small_h = (box.y2 - box.y1) * scale;
                        float small_x = center_x - small_w / 2.0f;
                        float small_y = center_y - small_h / 2.0f;
                        
                        cv::Rect small_rect(small_x, small_y, small_w, small_h);
                        cv::rectangle(resultImg, small_rect, boxColor, 2);
                        
                        std::string label = (box.class_id >= 0 && box.class_id < defectNames.size()) ? defectNames[box.class_id] : "unknown";
                        std::string prefix = (box.light_type == LightField::DarkField) ? "DF-" : "BL-";
                        std::string text = prefix + label + " " + std::to_string((int)(box.confidence * 100)) + "%";
                        
                        cv::putText(resultImg, text, cv::Point(small_x, small_y - 5), cv::FONT_HERSHEY_SIMPLEX, 0.8, lfBgColor, 2);
                        
                        if (box.passedHostAppFilter) {
                            float big_w = small_w * 5.0f;
                            float big_h = small_h * 5.0f;
                            float big_x = center_x - big_w / 2.0f;
                            float big_y = center_y - big_h / 2.0f;
                            
                            cv::Rect big_rect(big_x, big_y, big_w, big_h);
                            cv::rectangle(resultImg, big_rect, boxColor, 2);
                            
                            // 半透明遮罩
                            cv::Mat overlay;
                            resultImg.copyTo(overlay);
                            cv::rectangle(overlay, big_rect, boxColor, cv::FILLED);
                            cv::addWeighted(overlay, 0.2, resultImg, 0.8, 0, resultImg);
                            
                            int baseLine = 0;
                            cv::Size bigTextSize = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 2.0, 3, &baseLine);
                            cv::rectangle(resultImg, cv::Point(big_x, big_y - bigTextSize.height - 10), cv::Point(big_x + bigTextSize.width, big_y), lfBgColor, cv::FILLED);
                            cv::putText(resultImg, text, cv::Point(big_x, big_y - 5), cv::FONT_HERSHEY_SIMPLEX, 2.0, cv::Scalar(255, 255, 255), 3);
                        }
                    }

                    std::vector<uchar> resBuffer;
                    cv::imencode(".jpg", resultImg, resBuffer);
                    
                    QtConcurrent::run(&diskPool, [resultFile, buffer = std::move(resBuffer)]() {
                        QFile file(resultFile);
                        if (file.open(QIODevice::WriteOnly)) {
                            file.write((const char*)buffer.data(), buffer.size());
                            file.close();
                        }
                    });
                }
                
                pendingSaves--;
            });
    }

    int fIdx = (int)frame->frameIndex;
    int camId = frame->cameraId;

    // 1. 记录此相机在此帧的结果
    m_receivedCamerasPerFrame[fIdx].insert(camId);
    if (isNG) {
        m_ngFrameIndices.insert(fIdx);
    }

    // 2. 双相机都到达时，进行整片玻璃的最终统计
    if (m_receivedCamerasPerFrame[fIdx].size() == 2) {
        m_totalCount++;
        
        bool pieceIsNG = m_ngFrameIndices.contains(fIdx);
        if (pieceIsNG) {
            m_ngCount++;
            m_ngFrameIndices.remove(fIdx);
        }
        
        m_receivedCamerasPerFrame.remove(fIdx);

        // 更新计数 UI (总数、批次、饼图)
        m_lblPieceCount->setText(QString("第%1片\n(%2 ms)").arg(m_totalCount).arg(frame->processTimeMs));
        m_lblPieceCount->setStyleSheet("font-size: 28px; color: #333333; border: none;");
        m_lblTotal->setText(QString("%1片").arg(m_totalCount));
        m_pieChart->setStats(m_ngCount, m_totalCount - m_ngCount);

        // 状态更新为该整片玻璃的最终判定状态
        if (pieceIsNG) {
            m_lblStatus->setText("NG");
            m_lblStatus->setStyleSheet("font-size: 80px; font-weight: bold; color: #E53935; border: none;");
        } else {
            m_lblStatus->setText("OK");
            m_lblStatus->setStyleSheet("font-size: 80px; font-weight: bold; color: #00C853; border: none;");
        }
    } else {
        // 3. 只有一个相机到达时，先提供实时状态反馈，但不计入总数和饼图
        if (fIdx > m_currentFrameIndex) {
            m_currentFrameIndex = fIdx;
            if (isNG) {
                m_lblStatus->setText("NG");
                m_lblStatus->setStyleSheet("font-size: 80px; font-weight: bold; color: #E53935; border: none;");
            } else {
                m_lblStatus->setText("OK");
                m_lblStatus->setStyleSheet("font-size: 80px; font-weight: bold; color: #00C853; border: none;");
            }
        } else if (fIdx == m_currentFrameIndex) {
            // 如果此帧之前已经展现过，只有当这次到的相机检测为 NG 时才覆盖为 NG
            if (isNG) {
                m_lblStatus->setText("NG");
                m_lblStatus->setStyleSheet("font-size: 80px; font-weight: bold; color: #E53935; border: none;");
            }
        }
    }
}

void MainWindow::onMenuConfigClicked()
{
    QInputDialog dlg(this);
    dlg.setWindowTitle("需要管理员权限");
    dlg.setLabelText("请输入系统配置管理员密码:");
    dlg.setTextEchoMode(QLineEdit::Password);
    dlg.setWindowFlags(dlg.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    
    if (dlg.exec() == QDialog::Accepted) {
        QString password = dlg.textValue();
        if (password == "admin") {
            ConfigDialog configDlg(this);
            configDlg.exec();
        } else {
            QMessageBox::warning(this, "错误", "密码错误，无法进入系统配置！");
        }
    }
}

void MainWindow::onMenuDetectClicked()
{
    DetectDialog dlg(this);
    connect(&dlg, &DetectDialog::recipeChanged, this, &MainWindow::onRecipeChanged);
    dlg.exec();
}

void MainWindow::onMenuLogClicked()
{
    LogDialog dlg(this);
    dlg.exec();
}

void MainWindow::onTimeTabClicked(int id)
{
    // TODO: 当接入数据库后，这里按时间段查询历史数据
    // 目前仅展示本次运行的实时累计数据（非 mock）
    Q_UNUSED(id);

    m_lblPieceCount->setText(QString("第%1片").arg(m_totalCount));
    m_lblTotal->setText(QString("%1片").arg(m_totalCount));
    m_pieChart->setStats(m_ngCount, m_totalCount - m_ngCount);
}

void MainWindow::onDefectTagClicked()
{
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    
    int state = btn->property("defectState").toInt();
    state = (state + 1) % 3; // Cycle 0->1->2->0 (0:隐藏, 1:绿色检出, 2:红色报警发PLC)
    btn->setProperty("defectState", state);
    
    QString defectName = btn->text();
    
    // Save to recipe
    QStringList parts = defectName.split("-");
    if (parts.size() == 2) {
        QString lfName = parts[0];
        QString defName = parts[1];
        Recipe r = RecipeManager::instance().getRecipe(RecipeManager::instance().getCurrentRecipeName());
        r.lightFields[lfName].defectParams[defName].uiState = state;
        RecipeManager::instance().addOrUpdateRecipe(r);
        RecipeManager::instance().save("recipes.json");
    }

    if (state == 0) { // Gray (Off/Hidden)
        btn->setStyleSheet("QPushButton { background-color: #E0E0E0; color: #777777; border-radius: 4px; border: none; font-size: 12px; } QPushButton:hover { background-color: #CCCCCC; }");
        if (!m_hiddenDefects.contains(defectName)) {
            m_hiddenDefects.append(defectName);
        }
    } else if (state == 1) { // Green (On/Shown)
        btn->setStyleSheet("QPushButton { background-color: #00C853; color: white; border-radius: 4px; border: none; font-size: 12px; } QPushButton:hover { background-color: #00E676; }");
        m_hiddenDefects.removeAll(defectName);
    } else { // 2 - Red (Alarm/PLC)
        btn->setStyleSheet("QPushButton { background-color: #E53935; color: white; border-radius: 4px; border: none; font-size: 12px; } QPushButton:hover { background-color: #EF5350; }");
        m_hiddenDefects.removeAll(defectName);
    }
    
    if (m_imageWidgetLeft) m_imageWidgetLeft->setHiddenDefects(m_hiddenDefects);
    if (m_imageWidgetRight) m_imageWidgetRight->setHiddenDefects(m_hiddenDefects);
}

void MainWindow::onRecipeChanged(const QString& recipeName)
{
    m_statusTitle->setText("当前检测型号 - " + recipeName);
    updateDefectTags();
}

void MainWindow::updateDefectTags()
{
    m_hiddenDefects.clear();
    Recipe r = RecipeManager::instance().getRecipe(RecipeManager::instance().getCurrentRecipeName());
    
    for (auto it = m_tagButtons.begin(); it != m_tagButtons.end(); ++it) {
        QString defectName = it.key();
        QPushButton* btn = it.value();
        QStringList parts = defectName.split("-");
        int state = 1; // Default
        if (parts.size() == 2) {
            QString lfName = parts[0];
            QString defName = parts[1];
            if (r.lightFields.contains(lfName) && r.lightFields[lfName].defectParams.contains(defName)) {
                state = r.lightFields[lfName].defectParams[defName].uiState;
            }
        }
        
        btn->setProperty("defectState", state);
        if (state == 0) { // Gray (Off/Hidden)
            btn->setStyleSheet("QPushButton { background-color: #E0E0E0; color: #777777; border-radius: 4px; border: none; font-size: 12px; } QPushButton:hover { background-color: #CCCCCC; }");
            if (!m_hiddenDefects.contains(defectName)) {
                m_hiddenDefects.append(defectName);
            }
        } else if (state == 1) { // Green (On/Shown)
            btn->setStyleSheet("QPushButton { background-color: #00C853; color: white; border-radius: 4px; border: none; font-size: 12px; } QPushButton:hover { background-color: #00E676; }");
        } else { // 2 - Red (Alarm/PLC)
            btn->setStyleSheet("QPushButton { background-color: #E53935; color: white; border-radius: 4px; border: none; font-size: 12px; } QPushButton:hover { background-color: #EF5350; }");
        }
    }
    
    if (m_imageWidgetLeft) m_imageWidgetLeft->setHiddenDefects(m_hiddenDefects);
    if (m_imageWidgetRight) m_imageWidgetRight->setHiddenDefects(m_hiddenDefects);
}
