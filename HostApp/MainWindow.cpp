#include "MainWindow.h"
#include <QtNetwork/QNetworkProxy>
#include <QDebug>
#include <QFile>
#include <QDateTime>

#pragma comment(lib, "ws2_32.lib")
#include "modules/RecipeManager.h"
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
#include <QPainter>
#include <opencv2/opencv.hpp>
#include <opencv2/cudawarping.hpp>
#include <opencv2/cudaimgproc.hpp>
#include "modules/ConfigManager.h"
#include "widgets/Dialogs.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_totalCount(0), m_ngCount(0), m_currentFrameIndex(-1),
      m_imageWidgetLeft(nullptr), m_imageWidgetRight(nullptr), m_pieChart(nullptr)
{
    setupLightTheme();
    setupUi();

    m_lightCtrlSocket = new QTcpSocket(this);
    m_lightCtrlSocket->setProxy(QNetworkProxy::NoProxy); // Disable system proxy to avoid UnsupportedSocketOperationError
    m_lightCtrlReconnectTimer = new QTimer(this);
    m_lightCtrlReconnectTimer->setInterval(3000);
    connect(m_lightCtrlReconnectTimer, &QTimer::timeout, this, &MainWindow::connectLightController);
    connect(m_lightCtrlSocket, &QTcpSocket::disconnected, m_lightCtrlReconnectTimer, static_cast<void(QTimer::*)()>(&QTimer::start));
    connect(m_lightCtrlSocket, &QTcpSocket::connected, m_lightCtrlReconnectTimer, &QTimer::stop);
    
    #if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_lightCtrlSocket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError err) {
        qWarning() << "[LightController] Connection error:" << m_lightCtrlSocket->errorString() << "Code:" << err;
        m_lightCtrlReconnectTimer->start();
    });
    #else
    connect(m_lightCtrlSocket, static_cast<void(QTcpSocket::*)(QAbstractSocket::SocketError)>(&QTcpSocket::error), this, [this](QAbstractSocket::SocketError err) {
        qWarning() << "[LightController] Connection error:" << m_lightCtrlSocket->errorString() << "Code:" << err;
        m_lightCtrlReconnectTimer->start();
    });
    #endif

    connect(m_lightCtrlSocket, &QTcpSocket::connected, this, [this]() {
        qDebug() << "[LightController] Connected successfully!";
    });
    connect(m_lightCtrlSocket, &QTcpSocket::disconnected, this, [this]() {
        qWarning() << "[LightController] Disconnected. Will try to reconnect...";
    });

    // Init dual inference and camera threads
    for (int i = 0; i < 2; ++i) {
        InferThread* inferTh = new InferThread(this);
        connect(inferTh, &InferThread::inferenceCompleted, this, &MainWindow::onImageProcessed);
        
        if (!inferTh->initializeTRT("D:/model/bll_cold_back.engine", "D:/model/bll_cold_tilt.engine")) {
            qWarning() << "[MainWindow] Failed to initialize TRT models!";
        }
        
        inferTh->start();
        m_inferThreads.push_back(inferTh);

        CameraThread* camThread = new CameraThread(i, this);
        connect(camThread, &CameraThread::imageCaptured, this, &MainWindow::onRawImageCaptured, Qt::DirectConnection);
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
    
    // 同步初始化界面布局（特别是中间拼图窗口的状态）
    onRecipeChanged(m_runningRecipeName);
    
    // 连接光源控制器
    connectLightController();
}

MainWindow::~MainWindow()
{
    disconnectLightController();
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

    m_imageWidgetCenter = new ImageWidget(this);
    m_imageWidgetCenter->setStyleSheet("border: 1px solid #D0D0D0; background-color: #7A7A7A;");
    m_imageWidgetCenter->setAlignment(Qt::AlignCenter);
    m_imageWidgetCenter->hide(); // 默认隐藏，等加载 Recipe 后再决定
    imagesLayout->addWidget(m_imageWidgetCenter, 1);

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

void MainWindow::connectLightController()
{
    auto& lc = ConfigManager::instance().lightCtrl;
    if (lc.ipAddress.isEmpty() || lc.port == 0) return;
    
    if (m_lightCtrlSocket->state() == QAbstractSocket::UnconnectedState) {
        qDebug() << "[LightController] Connecting to" << lc.ipAddress << ":" << lc.port;
        m_lightCtrlSocket->connectToHost(lc.ipAddress, lc.port);
    }
}

void MainWindow::disconnectLightController()
{
    m_lightCtrlReconnectTimer->stop();
    if (m_lightCtrlSocket->state() == QAbstractSocket::ConnectedState) {
        m_lightCtrlSocket->disconnectFromHost();
    }
}

void MainWindow::setLightControllerLineCount(int lineCount)
{
    qDebug() << "[LightController] Variable line height mode (VFL) active. Ignoring setLightControllerLineCount.";
    /*
    if (m_lightCtrlSocket->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "[LightController] Not connected. Cannot set line count.";
        return;
    }
    
    // CST Protocol: SetLineSignalOutputData (0x81)
    // 信号数量 = 行高 / 10
    int signalCount = lineCount / 10;
    
    QByteArray cmd;
    cmd.append(static_cast<char>(0x81)); // 帧头
    cmd.append(static_cast<char>(0x01)); // 模式 (单工位)
    cmd.append(static_cast<char>(0x01)); // 工位 (仅需发送给1号工位，即可全局生效)
    
    // 延时 (高低) - 2字节大端
    int delay = 0;
    cmd.append(static_cast<char>((delay >> 8) & 0xFF));
    cmd.append(static_cast<char>(delay & 0xFF));
    
    // 信号数量 (高低) - 2字节大端
    cmd.append(static_cast<char>((signalCount >> 8) & 0xFF));
    cmd.append(static_cast<char>(signalCount & 0xFF));
    
    // 最低行触发间隔 - 1字节
    int interval = 5;
    cmd.append(static_cast<char>(interval & 0xFF));
    
    // BCC 校验 (前8个字节异或和)
    char bcc = 0;
    for (int i = 0; i < cmd.size(); ++i) {
        bcc ^= cmd[i];
    }
    cmd.append(bcc);
    
    m_lightCtrlSocket->write(cmd);
    m_lightCtrlSocket->flush();
    qDebug() << "[LightController] Sent 0x81 (Big Endian, 9 bytes) to Station 1. SignalCount =" << signalCount << "Hex:" << cmd.toHex();
    */
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
            connect(camThread, &CameraThread::imageCaptured, this, &MainWindow::onRawImageCaptured, Qt::DirectConnection);
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

void MainWindow::onRawImageCaptured(std::shared_ptr<ImageFrame> frame)
{
    if (!frame) return;

    if (frame->isForRoi) {
        // Should not reach here because capturedForRoi is connected separately, but just in case
        return;
    }

    int fIdx = frame->frameIndex;
    int camId = frame->cameraId;

    Recipe r = RecipeManager::instance().getRecipe(m_runningRecipeName);
    
    if (!r.enableStitchingMode) {
        frame->isStitchingMode = false;
        // 如果不拼图，直接推给对应的推理线程
        if (camId >= 0 && camId < m_inferThreads.size()) {
            m_inferThreads[camId]->pushImage(frame);
        }
        return;
    }

    // 拼图模式缓冲
    m_rawFrames[fIdx][camId] = frame;

    if (m_rawFrames[fIdx].size() == 2) {
        auto leftFrame = m_rawFrames[fIdx][0];
        auto rightFrame = m_rawFrames[fIdx][1];
        
        m_rawFrames.remove(fIdx); // 取出后立刻移除

        int overlap = ConfigManager::instance().camera.overlapPixels;

        // 1. 去交织 (Fast memcpy)
        int src_w = leftFrame->image.cols;
        int src_h = leftFrame->image.rows;
        int split_h = src_h / 2;

        cv::Mat leftBL(split_h, src_w, leftFrame->image.type());
        cv::Mat leftDF(split_h, src_w, leftFrame->image.type());
        cv::Mat rightBL(split_h, src_w, rightFrame->image.type());
        cv::Mat rightDF(split_h, src_w, rightFrame->image.type());

        size_t step = leftFrame->image.step;
        size_t out_step = leftBL.step; 
        size_t copy_bytes = src_w * leftFrame->image.elemSize();
        for (int r = 0; r < split_h; ++r) {
            memcpy(leftBL.data + r * out_step, leftFrame->image.data + (r * 2) * step, copy_bytes);
            memcpy(leftDF.data + r * out_step, leftFrame->image.data + (r * 2 + 1) * step, copy_bytes);
            memcpy(rightBL.data + r * out_step, rightFrame->image.data + (r * 2) * step, copy_bytes);
            memcpy(rightDF.data + r * out_step, rightFrame->image.data + (r * 2 + 1) * step, copy_bytes);
        }

        // 2. 裁剪重叠区
        int crop_w_left = src_w - overlap / 2;
        int crop_w_right = src_w - overlap / 2;
        int x_offset_right = overlap / 2;

        if (crop_w_left <= 0 || crop_w_right <= 0) {
            crop_w_left = src_w;
            crop_w_right = src_w;
            x_offset_right = 0;
        }

        cv::Mat leftBL_crop = leftBL(cv::Rect(0, 0, crop_w_left, split_h));
        cv::Mat leftDF_crop = leftDF(cv::Rect(0, 0, crop_w_left, split_h));
        cv::Mat rightBL_crop = rightBL(cv::Rect(x_offset_right, 0, crop_w_right, split_h));
        cv::Mat rightDF_crop = rightDF(cv::Rect(x_offset_right, 0, crop_w_right, split_h));

        // 3. 拼接 (GPU 零拷贝拼图)
        cv::cuda::GpuMat d_stitchedBL(split_h, crop_w_left + crop_w_right, leftBL_crop.type());
        cv::cuda::GpuMat d_stitchedDF(split_h, crop_w_left + crop_w_right, leftBL_crop.type());
        
        cv::cuda::GpuMat d_leftBL_crop, d_leftDF_crop, d_rightBL_crop, d_rightDF_crop;
        d_leftBL_crop.upload(leftBL_crop);
        d_leftDF_crop.upload(leftDF_crop);
        d_rightBL_crop.upload(rightBL_crop);
        d_rightDF_crop.upload(rightDF_crop);

        d_leftBL_crop.copyTo(d_stitchedBL(cv::Rect(0, 0, crop_w_left, split_h)));
        d_rightBL_crop.copyTo(d_stitchedBL(cv::Rect(crop_w_left, 0, crop_w_right, split_h)));
        d_leftDF_crop.copyTo(d_stitchedDF(cv::Rect(0, 0, crop_w_left, split_h)));
        d_rightDF_crop.copyTo(d_stitchedDF(cv::Rect(crop_w_left, 0, crop_w_right, split_h)));

        // 4. 轮廓提取与最小外接矩形 (基于 BL 图像)
        cv::cuda::GpuMat d_gray, d_smallGray;
        if (d_stitchedBL.channels() == 3) {
            cv::cuda::cvtColor(d_stitchedBL, d_gray, cv::COLOR_BGR2GRAY);
        } else {
            d_gray = d_stitchedBL;
        }
        
        // CUDA resize instead of CPU
        cv::cuda::resize(d_gray, d_smallGray, cv::Size(), 0.1, 0.1, cv::INTER_NEAREST);
        
        cv::Mat smallGray;
        d_smallGray.download(smallGray);
        
        cv::Mat mask;
        cv::threshold(smallGray, mask, 30, 255, cv::THRESH_BINARY);
        
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        cv::Mat finalBL, finalDF;

        if (!contours.empty()) {
            int maxIdx = 0;
            double maxArea = 0;
            for (int i = 0; i < contours.size(); ++i) {
                double a = cv::contourArea(contours[i]);
                if (a > maxArea) {
                    maxArea = a;
                    maxIdx = i;
                }
            }

            cv::RotatedRect smallRect = cv::minAreaRect(contours[maxIdx]);
            
            cv::RotatedRect minRect;
            minRect.center.x = smallRect.center.x * 10.0f;
            minRect.center.y = smallRect.center.y * 10.0f;
            minRect.size.width = smallRect.size.width * 10.0f;
            minRect.size.height = smallRect.size.height * 10.0f;
            minRect.angle = smallRect.angle;
            
            float angle = minRect.angle;
            float width = minRect.size.width * 1.2f; // 拓宽 20%
            float height = minRect.size.height * 1.2f;
            
            if (width > height) {
                angle += 90.0f;
                std::swap(width, height);
            }
            
            cv::Mat M = cv::getRotationMatrix2D(minRect.center, angle, 1.0);
            
            // 平移修正：相当于融合了 getRectSubPix 的操作
            M.at<double>(0, 2) -= (minRect.center.x - width / 2.0);
            M.at<double>(1, 2) -= (minRect.center.y - height / 2.0);
            
            cv::cuda::GpuMat d_rotatedBL, d_rotatedDF;
            cv::cuda::warpAffine(d_stitchedBL, d_rotatedBL, M, cv::Size(width, height), cv::INTER_CUBIC, cv::BORDER_CONSTANT, cv::Scalar(0));
            cv::cuda::warpAffine(d_stitchedDF, d_rotatedDF, M, cv::Size(width, height), cv::INTER_CUBIC, cv::BORDER_CONSTANT, cv::Scalar(0));
            
            d_rotatedBL.download(finalBL);
            d_rotatedDF.download(finalDF);
        } else {
            d_stitchedBL.download(finalBL);
            d_stitchedDF.download(finalDF);
        }

        // 5. 重新交织为一张新帧 (Fast memcpy)
        int final_w = finalBL.cols;
        int final_h = finalBL.rows;
        
        cv::Mat reinterleaved(final_h * 2, final_w, finalBL.type());
        size_t r_step = finalBL.step;
        size_t dst_step = reinterleaved.step;
        size_t final_copy_bytes = final_w * finalBL.elemSize();
        for (int r = 0; r < final_h; ++r) {
            memcpy(reinterleaved.data + (r * 2) * dst_step, finalBL.data + r * r_step, final_copy_bytes);
            memcpy(reinterleaved.data + (r * 2 + 1) * dst_step, finalDF.data + r * r_step, final_copy_bytes);
        }

        auto newFrame = std::make_shared<ImageFrame>();
        newFrame->cameraId = 0; // 统一用 0 通道推理
        newFrame->frameIndex = fIdx;
        newFrame->image = reinterleaved;
        newFrame->exposureType = 0;
        newFrame->isStitchingMode = true;

        m_inferThreads[0]->pushImage(newFrame);
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
    
    int fIdx = frame->frameIndex;
    int camId = frame->cameraId;

    // 提取通用保存逻辑到一个 lambda
    auto processSaveForCamera = [&](const std::shared_ptr<ImageFrame>& frameData, const cv::Mat& blData, const cv::Mat& dfData, ImageWidget* widget, const QString& camStr) {
        bool isNG = false;
        QStringList defectNames = {"崩边", "划伤", "黑划伤", "凹凸点", "不可擦拭脏污", "点伤", "异色"};
        for (const auto& res : frameData->results) {
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

            // 将耗时的保存任务扔给后台
            QtConcurrent::run(&compressPool, [frame = frameData, rootPath, dateStr, batchStr, timestamp, seqStr, isNG, camStr, imgBL = blData, imgDF = dfData, hiddenDefects = m_hiddenDefects]() {
                auto saveField = [&](const cv::Mat& img, LightField lf, const QString& lfStr) {
                    if (img.empty()) return;
                        
                    QString oriDir = isNG ? QString("%1/%2/%3/NG_ori/%4").arg(rootPath).arg(dateStr).arg(batchStr).arg(lfStr)
                                          : QString("%1/%2/%3/OK_ori/%4").arg(rootPath).arg(dateStr).arg(batchStr).arg(lfStr);
                    QDir().mkpath(oriDir);
                    
                    QString filenameBase;
                    if (camStr.isEmpty()) {
                        filenameBase = QString("%1_%2_%3_%4").arg(seqStr).arg(timestamp).arg(lfStr).arg(isNG ? "NG" : "OK");
                    } else {
                        filenameBase = QString("%1_%2_%3_%4_%5").arg(seqStr).arg(camStr).arg(timestamp).arg(lfStr).arg(isNG ? "NG" : "OK");
                    }
                    
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

                if (isNG && !imgBL.empty()) {
                    QString resultDir = QString("%1/%2/%3/NG_result").arg(rootPath).arg(dateStr).arg(batchStr);
                    QDir().mkpath(resultDir);
                    QString filenameBase;
                    if (camStr.isEmpty()) {
                        filenameBase = QString("%1_%2_Result_NG").arg(seqStr).arg(timestamp);
                    } else {
                        filenameBase = QString("%1_%2_%3_Result_NG").arg(seqStr).arg(camStr).arg(timestamp);
                    }
                    QString resultFile = QString("%1/%2.jpg").arg(resultDir).arg(filenameBase);
                    
                    // 1. Convert to color for drawing (use RGB for QImage)
                    cv::Mat annotatedBL;
                    if (imgBL.channels() == 1) {
                        cv::cvtColor(imgBL, annotatedBL, cv::COLOR_GRAY2RGB);
                    } else {
                        cv::cvtColor(imgBL, annotatedBL, cv::COLOR_BGR2RGB);
                    }
                    
                    // Copy required data for background thread
                    std::vector<DefectResult> results = frame->results;
                    
                    // 2. Draw defects exactly like UI using QPainter in background thread
                    QtConcurrent::run(&diskPool, [resultFile, annotatedBL, results, hiddenDefects]() mutable {
                        // Create QImage wrapper around annotatedBL
                        QImage img(annotatedBL.data, annotatedBL.cols, annotatedBL.rows, annotatedBL.step, QImage::Format_RGB888);
                        
                        QPainter painter(&img);
                        painter.setRenderHint(QPainter::Antialiasing);
                        
                        QStringList defectNames = {"崩边", "划伤", "黑划伤", "凹凸点", "不可擦拭脏污", "点伤", "异色"};
                        QVector<QColor> defectColors = {
                            QColor("#00FFFF"), QColor("#FF00FF"), QColor("#8A2BE2"), 
                            QColor("#FFA500"), QColor("#8B4513"), QColor("#FF69B4"), QColor("#32CD32")
                        };
                        
                        for (const auto& box : results) {
                            QString label = (box.class_id >= 0 && box.class_id < defectNames.size()) ? defectNames[box.class_id] : "unknown";
                            QString lfPrefix = (box.light_type == LightField::DarkField) ? "暗场-" : "背光-";
                            QString fullLabel = lfPrefix + label;
                            
                            if (hiddenDefects.contains(fullLabel)) continue;
                            
                            QColor boxColor = (box.class_id >= 0 && box.class_id < defectColors.size()) ? defectColors[box.class_id] : Qt::white;
                            QColor lfBgColor = (box.light_type == LightField::DarkField) ? QColor("#D4AF37") : QColor("#DC143C");
                            Qt::PenStyle penStyle = (box.light_type == LightField::DarkField) ? Qt::DashLine : Qt::SolidLine;
                            
                            float box_w = box.x2 - box.x1;
                            float box_h = box.y2 - box.y1;
                            float center_x = box.x1 + box_w / 2.0f;
                            float center_y = box.y1 + box_h / 2.0f;
                            
                            const float MM_PER_PX = 0.039f;
                            float area_mm2 = box_w * box_h * MM_PER_PX * MM_PER_PX;
                            float diag_mm  = std::sqrt(box_w * box_w + box_h * box_h) * MM_PER_PX;
                            
                            QString displayText = QString("%1  %2%  S:%3  D:%4")
                                .arg(label)
                                .arg((int)(box.confidence * 100))
                                .arg(area_mm2, 0, 'f', 1)
                                .arg(diag_mm,  0, 'f', 1);
                                
                            if (box.passedHostAppFilter) {
                                float big_draw_w = box_w * 1.2f;
                                float big_draw_h = box_h * 1.2f;
                                float big_draw_x = center_x - big_draw_w / 2.0f;
                                float big_draw_y = center_y - big_draw_h / 2.0f;
                                
                                QColor fillColor = boxColor;
                                fillColor.setAlpha(50);
                                painter.setBrush(fillColor);
                                QPen bigBorderPen(boxColor, 12, penStyle);
                                painter.setPen(bigBorderPen);
                                painter.drawRect(QRectF(big_draw_x, big_draw_y, big_draw_w, big_draw_h));
                                
                                QFont fBig = painter.font();
                                fBig.setPixelSize(200);
                                fBig.setBold(true);
                                painter.setFont(fBig);
                                
                                QFontMetrics fm(fBig);
                                QRectF textRect = fm.boundingRect(displayText);
                                QRectF textBgRect(big_draw_x, big_draw_y - textRect.height() - 10, textRect.width() + 20, textRect.height() + 10);
                                
                                painter.setBrush(lfBgColor);
                                painter.setPen(Qt::NoPen);
                                painter.drawRect(textBgRect);
                                
                                painter.setPen(Qt::white);
                                painter.drawText(big_draw_x + 10, big_draw_y - 10, displayText);
                            } else {
                                QColor smallBoxColor = boxColor;
                                smallBoxColor.setAlpha(180);
                                QPen smallBorderPen(smallBoxColor, 2, penStyle);
                                painter.setPen(smallBorderPen);
                                painter.setBrush(Qt::NoBrush);
                                painter.drawRect(QRectF(box.x1, box.y1, box_w, box_h));
                                
                                QFont fSmall = painter.font();
                                fSmall.setPixelSize(20);
                                fSmall.setBold(true);
                                painter.setFont(fSmall);
                                
                                QColor smallLfBgColor = lfBgColor;
                                smallLfBgColor.setAlpha(180);
                                painter.setPen(smallLfBgColor);
                                painter.drawText(box.x1 + 5, box.y1 - 5, displayText);
                            }
                        }
                        
                        painter.end();
                        img.save(resultFile, "JPG", 80);
                    });
                }
            });
        }
    };
    
    // 取决于帧产生时的状态，而非当前的全局 Recipe 设置，避免运行时切换模式导致管线错乱
    if (frame->isStitchingMode) {
        m_imageWidgetCenter->updateImage(frame, imgBL, imgDF);
        processSaveForCamera(frame, imgBL, imgDF, m_imageWidgetCenter, "");
        
        bool pieceIsNG = m_ngFrameIndices.contains(fIdx);
        m_currentFrameIndex = fIdx;
        m_totalCount++;
        if (pieceIsNG) m_ngCount++;
        
        m_lblPieceCount->setText(QString("第%1片\n(%2 ms)").arg(m_totalCount).arg(frame->processTimeMs));
        m_lblPieceCount->setStyleSheet("font-size: 28px; color: #333333; border: none;");
        m_lblTotal->setText(QString("%1片").arg(m_totalCount));
        m_pieChart->setStats(m_ngCount, m_totalCount - m_ngCount);

        if (pieceIsNG) {
            m_lblStatus->setText("NG");
            m_lblStatus->setStyleSheet("font-size: 80px; font-weight: bold; color: #E53935; border: none;");
        } else {
            m_lblStatus->setText("OK");
            m_lblStatus->setStyleSheet("font-size: 80px; font-weight: bold; color: #00C853; border: none;");
        }
        return;
    }

    // 1. 将当前相机的帧数据暂存
    m_pendingFrames[fIdx][camId] = {frame, imgBL, imgDF};

    // 2. 硬件掉线/触发同步异常检测机制 (Watchdog)
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

    // 4. 同步触发两张图的保存和 NG 检测
    processSaveForCamera(data0.frame, data0.imgBL, data0.imgDF, m_imageWidgetLeft, QString("Camera_%1").arg(data0.frame->cameraId));
    processSaveForCamera(data1.frame, data1.imgBL, data1.imgDF, m_imageWidgetRight, QString("Camera_%1").arg(data1.frame->cameraId));

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
    
    // 下发行高给光源控制器（取左右相机的最大高度）
    int maxLineCount = qMax(r.roiHeightLeft, r.roiHeightRight);
    // setLightControllerLineCount(maxLineCount); // 不定行高模式下不再发送固定行数指令
    
    // 切换 UI 拼图模式
    if (r.enableStitchingMode) {
        if (m_imageWidgetLeft) m_imageWidgetLeft->hide();
        if (m_imageWidgetRight) m_imageWidgetRight->hide();
        if (m_imageWidgetCenter) m_imageWidgetCenter->show();
    } else {
        if (m_imageWidgetLeft) m_imageWidgetLeft->show();
        if (m_imageWidgetRight) m_imageWidgetRight->show();
        if (m_imageWidgetCenter) m_imageWidgetCenter->hide();
    }

    for (int i = 0; i < m_cameraThreads.size(); ++i) {
        auto cam = m_cameraThreads[i];
        if (cam) {
            if (i == 0) {
                cam->updateRoi(r.offsetXLeft, r.offsetYLeft, r.roiWidthLeft);
            } else {
                cam->updateRoi(r.offsetXRight, r.offsetYRight, r.roiWidthRight);
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
