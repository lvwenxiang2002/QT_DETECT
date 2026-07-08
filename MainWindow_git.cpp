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
        connect(camThread, &CameraThread::imageCaptured, this, &MainWindow::capturedForRoi, Qt::QueuedConnection);
        connect(camThread, &CameraThread::errorOccurred, this, &MainWindow::onCameraError, Qt::QueuedConnection);
        m_cameraThreads.push_back(camThread);
        
        // 初始化时立刻启动线程，让相机尽早完成打开和配置，避免第一次点击开始时卡顿
        camThread->start();
    }
    
    updateDefectTags();
    
    // 初始化时，记录当前的型号名字，并创建一个新的批次
    m_runningRecipeName = RecipeManager::instance().getCurrentRecipeName();
    createNewBatch();
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
    
    m_statusTitle = new QLabel(this);
    updateStatusLight(m_currentStatus);
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
    // 如果系统处于报警停机状态，点击开始时，我们需要强制销毁并重启相机线程，以重新初始化硬件（这会触发 ItkDevOpen 等底层 API）
    if (m_currentStatus == Status_Error) {
        qDebug() << "[MainWindow] Re-initializing cameras after error state.";
        for (auto cam : m_cameraThreads) {
            cam->stopCapture();
            cam->quit();
            cam->wait();
            delete cam;
        }
        m_cameraThreads.clear();
        
        for (int i = 0; i < 2; ++i) {
            CameraThread* camThread = new CameraThread(i, this);
            connect(camThread, &CameraThread::imageCaptured, m_inferThreads[i], &InferThread::pushImage, Qt::DirectConnection);
            connect(camThread, &CameraThread::imageCaptured, this, &MainWindow::capturedForRoi, Qt::QueuedConnection);
            connect(camThread, &CameraThread::errorOccurred, this, &MainWindow::onCameraError, Qt::QueuedConnection);
            m_cameraThreads.push_back(camThread);
            
            camThread->start(); // 立即跑底层初始化
        }
        
        // 确保配置是最新的
        onRecipeChanged(m_runningRecipeName);
    }

    QList<QPushButton*> allBtns = this->findChildren<QPushButton*>();
    for (QPushButton* btn : allBtns) {
        if (btn != m_btnStop) {
            btn->setEnabled(false);
        }
    }
    m_btnStop->setEnabled(true);

    m_pendingFrames.clear();

    updateStatusLight(Status_Running);

    // 原本在这里的清零逻辑已经移除。现在的暂停和开始只会控制相机的采图，不会清零计数器。
    
    for (auto cam : m_cameraThreads) {
        cam->resetFrameCounter();
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
    
    updateStatusLight(Status_Stopped);
    
    for (auto cam : m_cameraThreads) {
        cam->stopCapture();
    }
}

void MainWindow::triggerCameraForRoi(int cameraId) {
    if (cameraId >= 0 && cameraId < m_cameraThreads.size()) {
        bool wasRunning = m_cameraThreads[cameraId]->isRunning();
        if (!wasRunning) {
            m_cameraThreads[cameraId]->startCapture(true);
        } else {
            // Already running, mark next frame for ROI
            m_cameraThreads[cameraId]->startCapture(true);
        }
    }
}

void MainWindow::stopCameraForRoi(int cameraId) {
    if (cameraId >= 0 && cameraId < m_cameraThreads.size()) {
        // If the main Stop button is NOT enabled, it means we are NOT in normal detection mode.
        // We should stop the camera to prevent it from continuously capturing ROI frames.
        if (!m_btnStop->isEnabled()) {
            m_cameraThreads[cameraId]->stopCapture();
        }
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
    
    int fIdx = (int)frame->frameIndex;
    int camId = frame->cameraId;
    
    // 1. 将当前相机的帧数据暂存
    m_pendingFrames[fIdx][camId] = {frame, imgBL, imgDF};

    // 2. 硬件掉线/触发同步异常检测机制 (Watchdog)
    // 如果某一个相机的信号线拔掉，它的回调将停止，但另一个健康的相机会继续疯跑推图
    // 当积压的未成对帧数超过 3 个时，判定为单边掉线或触发严重错位！
    if (m_pendingFrames.size() > 3) {
        onCameraError("相机触发同步异常或一侧信号掉线，已紧急停机！");
        m_pendingFrames.clear();
        return;
    }

    // 3. 如果同一片的两个相机数据还没凑齐，就先等待
    if (m_pendingFrames[fIdx].size() < 2) {
        return;
    }

    // --- 此时两个相机的数据都已经准备好了，我们开始严格同步执行！ ---
    
    auto data0 = m_pendingFrames[fIdx][0];
    auto data1 = m_pendingFrames[fIdx][1];

    // 3. 同时更新左右两侧的 UI 界面，保证绝对的视觉同步！
    m_imageWidgetLeft->updateImage(data0.frame, data0.imgBL, data0.imgDF);
    m_imageWidgetRight->updateImage(data1.frame, data1.imgBL, data1.imgDF);

    // 提取通用保存逻辑到一个 lambda
    auto processSaveForCamera = [&](const PendingFrameData& data, ImageWidget* widget) {
        bool isNG = false;
        QStringList defectNames = {"崩边", "划伤", "黑划伤", "凹凸点", "不可擦拭脏污", "点伤", "异色"};
        for (const auto& res : data.frame->results) {
            QString label = (res.class_id >= 0 && res.class_id < defectNames.size()) ? defectNames[res.class_id] : "unknown";
            QString lfPrefix = (res.light_type == LightField::DarkField) ? "暗场-" : "背光-";
            QString fullLabel = lfPrefix + label;
            if (res.passedHostAppFilter && !m_hiddenDefects.contains(fullLabel)) {
                isNG = true;
                break;
            }
        }

        if (isNG) {
            m_ngFrameIndices.insert(fIdx);
        }

        if (ConfigManager::instance().storage.saveStrategy == 0 ||
            (ConfigManager::instance().storage.saveStrategy == 1 && isNG)) 
        {
            QString rootPath = ConfigManager::instance().storage.savePath; 
            QString dateStr = QDateTime::currentDateTime().toString("yyyy-MM-dd");
            QString timestamp = QDateTime::currentDateTime().toString("HHmmss_zzz");
            QString camStr = QString("Camera_%1").arg(data.frame->cameraId);
            QString seqStr = QString("%1").arg(m_totalCount + 1, 5, 10, QChar('0'));
            QString batchStr = m_currentBatchDir;
            
            static QThreadPool compressPool;
            static QThreadPool diskPool;
            static bool poolInit = false;
            if (!poolInit) {
                compressPool.setMaxThreadCount(6);
                diskPool.setMaxThreadCount(6);
                poolInit = true;
            }

            QImage uiImage;
            if (isNG) {
                // 在主线程同步截取刚更新好的 UI 画面
                uiImage = widget->grabSceneImage();
            }

            // 将耗时的保存任务扔给后台
            QtConcurrent::run(&compressPool, [frame = data.frame, rootPath, dateStr, batchStr, timestamp, seqStr, isNG, camStr, imgBL = data.imgBL, imgDF = data.imgDF, uiImage]() {
                auto saveField = [&](const cv::Mat& img, LightField lf, const QString& lfStr) {
                    if (img.empty()) return;
                        
                    QString oriDir = isNG ? QString("%1/%2/%3/NG_ori/%4").arg(rootPath).arg(dateStr).arg(batchStr).arg(lfStr)
                                          : QString("%1/%2/%3/OK_ori/%4").arg(rootPath).arg(dateStr).arg(batchStr).arg(lfStr);
                    QDir().mkpath(oriDir);
                    QString filenameBase = QString("%1_%2_%3_%4_%5").arg(seqStr).arg(camStr).arg(timestamp).arg(lfStr).arg(isNG ? "NG" : "OK");
                    
                    QString oriFile = QString("%1/%2.png").arg(oriDir).arg(filenameBase);
                    std::vector<int> compression_params = {cv::IMWRITE_PNG_COMPRESSION, 1};
                    
                    std::vector<uchar> oriBuffer;
                    cv::imencode(".png", img, oriBuffer, compression_params);
                    
                    QtConcurrent::run(&diskPool, [oriFile, buffer = std::move(oriBuffer)]() {
                        QFile file(oriFile);
                        if (file.open(QIODevice::WriteOnly)) {
                            file.write((const char*)buffer.data(), buffer.size());
                            file.close();
                        }
                    });
                };
                
                saveField(imgBL, LightField::Backlight, "Backlight");
                saveField(imgDF, LightField::DarkField, "Darkfield");

                if (isNG && !uiImage.isNull()) {
                    QString resultDir = QString("%1/%2/%3/NG_result").arg(rootPath).arg(dateStr).arg(batchStr);
                    QDir().mkpath(resultDir);
                    QString filenameBase = QString("%1_%2_%3_Result_NG").arg(seqStr).arg(camStr).arg(timestamp);
                    QString resultFile = QString("%1/%2.jpg").arg(resultDir).arg(filenameBase);
                    
                    QByteArray ba;
                    QBuffer buffer(&ba);
                    buffer.open(QIODevice::WriteOnly);
                    uiImage.save(&buffer, "JPG", 80); 
                    
                    QtConcurrent::run(&diskPool, [resultFile, data = std::move(ba)]() {
                        QFile file(resultFile);
                        if (file.open(QIODevice::WriteOnly)) {
                            file.write(data);
                            file.close();
                        }
                    });
                }
            });
        }
    };

    // 4. 同步触发两张图的保存和 NG 检测
    processSaveForCamera(data0, m_imageWidgetLeft);
    processSaveForCamera(data1, m_imageWidgetRight);

    // 5. 统一更新计数器和饼图
    m_totalCount++;
    bool pieceIsNG = m_ngFrameIndices.contains(fIdx);
    if (pieceIsNG) {
        m_ngCount++;
        m_ngFrameIndices.remove(fIdx);
    }

    m_pendingFrames.remove(fIdx);

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

void MainWindow::createNewBatch()
{
    // 使用时间戳（时分秒）作为批次文件夹名字
    m_currentBatchDir = QDateTime::currentDateTime().toString("HHmmss");
    
    // 重置所有计数和双相机对齐追踪变量
    m_totalCount = 0;
    m_ngCount = 0;
    m_currentFrameIndex = -1;
    m_pendingFrames.clear();
    m_ngFrameIndices.clear();
    m_lblPieceCount->setText("第0片");
    m_lblStatus->setText("--");
    m_lblStatus->setStyleSheet("font-size: 80px; font-weight: bold; color: #CCCCCC; border: none;");
    m_lblTotal->setText("0片");
    m_pieChart->setStats(0, 0);
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

void MainWindow::updateStatusLight(SystemStatus status) {
    m_currentStatus = status;
    QString colorStr = "#777777"; // default gray
    if (status == Status_Running) colorStr = "#00C853"; // green
    else if (status == Status_Error) colorStr = "#E53935"; // red
    
    m_statusTitle->setText(QString("<font color='%1'>●</font> 当前检测 - %2")
                           .arg(colorStr)
                           .arg(RecipeManager::instance().getCurrentRecipeName()));
}

void MainWindow::onCameraError(QString msg) {
    updateStatusLight(Status_Error);
    
    // 紧急停止所有硬件动作，防止后台继续空转推理
    for (auto cam : m_cameraThreads) {
        cam->stopCapture();
    }
    
    // 恢复 UI 按钮的交互状态
    QList<QPushButton*> allBtns = this->findChildren<QPushButton*>();
    for (QPushButton* btn : allBtns) {
        btn->setEnabled(true);
    }
    m_btnStop->setEnabled(false);
    
    // 可以在状态栏或日志中输出，或者弹窗
    qWarning() << "[MainWindow] Error triggered:" << msg;
    QMessageBox::critical(this, "严重错误", msg);
}

void MainWindow::onRecipeChanged(const QString& recipeName)
{
    updateStatusLight(m_currentStatus);
    updateDefectTags();
    
    // 更新相机 ROI 和曝光时间
    Recipe r = RecipeManager::instance().getRecipe(recipeName);
    for (int i = 0; i < m_cameraThreads.size(); ++i) {
        auto cam = m_cameraThreads[i];
        if (cam) {
            if (i == 0) {
                cam->updateRoi(r.offsetXLeft, r.offsetYLeft, r.roiWidthLeft, r.roiHeightLeft);
                cam->updateExposure(r.exposureTimeLeft);
            } else {
                cam->updateRoi(r.offsetXRight, r.offsetYRight, r.roiWidthRight, r.roiHeightRight);
                cam->updateExposure(r.exposureTimeRight);
            }
        }
    }
    
    // 只有在真的【切换】了不同型号时，才新建批次并清零计数
    // 如果只是在当前型号下修改了阈值并保存，不应该清零批次！
    if (m_runningRecipeName != recipeName) {
        m_runningRecipeName = recipeName;
        createNewBatch();
    }
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
