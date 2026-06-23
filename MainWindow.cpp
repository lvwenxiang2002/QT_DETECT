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
    : QMainWindow(parent), m_totalCount(0), m_ngCount(0),
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
        connect(camThread, &CameraThread::imageCaptured, inferTh, &InferThread::pushImage);
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
    m_btnStart->setEnabled(false);
    m_btnStop->setEnabled(true);
    m_btnConfig->setEnabled(false);
    m_btnDetect->setEnabled(false);
    m_btnLog->setEnabled(false);
    for (auto btn : m_tagButtons.values()) {
        btn->setEnabled(false);
    }
    
    for (auto cam : m_cameraThreads) {
        cam->startCapture();
    }
}

void MainWindow::onStopClicked()
{
    m_btnStart->setEnabled(true);
    m_btnStop->setEnabled(false);
    m_btnConfig->setEnabled(true);
    m_btnDetect->setEnabled(true);
    m_btnLog->setEnabled(true);
    for (auto btn : m_tagButtons.values()) {
        btn->setEnabled(true);
    }
    
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
        QString rootPath = ConfigManager::instance().storage.savePath;
        QString dateStr = QDateTime::currentDateTime().toString("yyyy-MM-dd");
        QString timestamp = QDateTime::currentDateTime().toString("HHmmss_zzz");
        QString camStr = QString("Camera_%1").arg(frame->cameraId);

        // Execute saving in a background thread pool to prevent blocking the UI
        QtConcurrent::run([frame, rootPath, dateStr, timestamp, isNG, camStr, imgBL, imgDF]() {
            auto saveField = [&](const cv::Mat& img, LightField lf, const QString& lfStr) {
                if (img.empty()) return;
                
                QString oriDir = isNG ? QString("%1/%2/NG_ori/%3/%4").arg(rootPath).arg(dateStr).arg(camStr).arg(lfStr)
                                      : QString("%1/%2/OK_ori/%3/%4").arg(rootPath).arg(dateStr).arg(camStr).arg(lfStr);
                QString resultDir = QString("%1/%2/NG_result/%3/%4").arg(rootPath).arg(dateStr).arg(camStr).arg(lfStr);
                
                QDir().mkpath(oriDir);
                QString filenameBase = QString("%1_%2_%3_%4").arg(timestamp).arg(camStr).arg(lfStr).arg(isNG ? "NG" : "OK");
                
                // 1. Save original image as PNG
                QString oriFile = QString("%1/%2.png").arg(oriDir).arg(filenameBase);
                cv::imwrite(oriFile.toLocal8Bit().constData(), img);
                
                // 2. Save result image if NG as JPG
                if (isNG) {
                    QDir().mkpath(resultDir);
                    cv::Mat resultImg;
                    if (img.channels() == 1) {
                        cv::cvtColor(img, resultImg, cv::COLOR_GRAY2BGR);
                    } else {
                        resultImg = img.clone();
                    }

                    std::vector<std::string> defectNames = {
                        "broken", "scratch", "scratch_black", "pit", 
                        "squalidity", "spot_defect", "discoloration"
                    };

                    std::vector<cv::Scalar> cvDefectColors = {
                        cv::Scalar(255, 255, 0),    // 0: 崩边 (Cyan -> BGR)
                        cv::Scalar(255, 0, 255),    // 1: 划伤 (Magenta)
                        cv::Scalar(226, 43, 138),   // 2: 黑划伤 (BlueViolet)
                        cv::Scalar(0, 165, 255),    // 3: 凹凸点 (Orange)
                        cv::Scalar(19, 69, 139),    // 4: 不可擦拭脏污 (SaddleBrown)
                        cv::Scalar(180, 105, 255),  // 5: 点伤 (HotPink)
                        cv::Scalar(50, 205, 50)     // 6: 异色 (LimeGreen)
                    };

                    for (const auto& box : frame->results) {
                        if (box.light_type != lf) continue; // Only draw boxes for this light field
                        
                        cv::Scalar boxColor = (box.class_id >= 0 && box.class_id < cvDefectColors.size()) ? cvDefectColors[box.class_id] : cv::Scalar(255, 255, 255);
                        cv::Scalar lfBgColor = (box.light_type == LightField::DarkField) ? cv::Scalar(55, 175, 212) : cv::Scalar(60, 20, 220); // BGR: Gold/Red
                        
                        float center_x = box.x1 + (box.x2 - box.x1) / 2.0f;
                        float center_y = box.y1 + (box.y2 - box.y1) / 2.0f;
                        float draw_w = box.x2 - box.x1;
                        float draw_h = box.y2 - box.y1;
                        int pen_width = 4;
                        double font_scale = 1.5;
                        int font_thickness = 3;
                        
                        if (box.passedHostAppFilter) {
                            draw_w *= 5.0f;
                            draw_h *= 5.0f;
                            pen_width = 12;
                            font_scale = 7.5; // 1.5 * 5
                            font_thickness = 10;
                        } else {
                            pen_width = 2;
                            font_scale = 0.8;
                            font_thickness = 2;
                        }
                        
                        float draw_x = center_x - draw_w / 2.0f;
                        float draw_y = center_y - draw_h / 2.0f;
                        cv::Rect rect(draw_x, draw_y, draw_w, draw_h);
                        cv::rectangle(resultImg, rect, boxColor, pen_width);
                        
                        std::string label = (box.class_id >= 0 && box.class_id < defectNames.size()) ? defectNames[box.class_id] : "unknown";
                        std::string prefix = (box.light_type == LightField::DarkField) ? "DF-" : "BL-";
                        std::string text = prefix + label + " " + std::to_string((int)(box.confidence * 100)) + "%";
                        
                        int baseLine = 0;
                        cv::Size textSize = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, font_scale, font_thickness, &baseLine);
                        cv::rectangle(resultImg, cv::Point(draw_x, draw_y - textSize.height - 10), cv::Point(draw_x + textSize.width, draw_y), lfBgColor, cv::FILLED);
                        cv::putText(resultImg, text, cv::Point(draw_x, draw_y - 5), cv::FONT_HERSHEY_SIMPLEX, font_scale, cv::Scalar(255, 255, 255), font_thickness);
                    }

                    QString resultFile = QString("%1/%2.jpg").arg(resultDir).arg(filenameBase);
                    cv::imwrite(resultFile.toLocal8Bit().constData(), resultImg);
                }
            };
            
            saveField(imgBL, LightField::Backlight, "Backlight");
            saveField(imgDF, LightField::DarkField, "Darkfield");
        });
    }

    // Update Stats (only increment total count on Camera 0 to avoid double counting)
    if (frame->cameraId == 0) {
        m_totalCount++;
    }
    // For NG count, to prevent double counting if both cameras detect NG on the same piece,
    // we use a simple static tracking of the frame index (assuming frameIndex is synced)
    static int lastNgFrameIndex = -1;
    if (isNG && (int)frame->frameIndex != lastNgFrameIndex) {
        m_ngCount++;
        lastNgFrameIndex = frame->frameIndex;
    }
    
    m_lblPieceCount->setText(QString("第%1片\n(%2 ms)").arg(m_totalCount).arg(frame->processTimeMs));
    m_lblPieceCount->setStyleSheet("font-size: 28px; color: #333333; border: none;"); // slightly adjust font size to fit 2 lines
    if (isNG) {
        m_lblStatus->setText("NG");
        m_lblStatus->setStyleSheet("font-size: 80px; font-weight: bold; color: #E53935; border: none;");
    } else {
        m_lblStatus->setText("OK");
        m_lblStatus->setStyleSheet("font-size: 80px; font-weight: bold; color: #00C853; border: none;");
    }

    m_lblTotal->setText(QString("%1片").arg(m_totalCount));
    m_pieChart->setStats(m_ngCount, m_totalCount - m_ngCount);
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
