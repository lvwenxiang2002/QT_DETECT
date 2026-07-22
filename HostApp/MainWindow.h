#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <vector>
#include <memory>
#include <QMap>
#include <QSet>
#include <QLabel>
#include <QTime>
#include <QtNetwork/QTcpSocket>
#include <QTimer>
#include "widgets/ImageWidget.h"
#include "widgets/PieChartWidget.h"
#include "modules/CameraThread.h"
#include "modules/InferThread.h"
#include "modules/ConfigManager.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
    enum SystemStatus {
        Status_Stopped = 0,
        Status_Running,
        Status_Error
    };
    void updateStatusLight(SystemStatus status);

signals:
    void capturedForRoi(std::shared_ptr<ImageFrame> frame);

public slots:
    Q_INVOKABLE void triggerCameraForRoi(int cameraId);
    Q_INVOKABLE void stopCameraForRoi(int cameraId);

private slots:
    void onStartClicked();
    void onStopClicked();
    void onCameraError(QString msg);
    void onRawImageCaptured(std::shared_ptr<ImageFrame> frame);
    void onImageProcessed(std::shared_ptr<ImageFrame> frame);

    // New slots for interaction
    void onMenuConfigClicked();
    void onMenuDetectClicked();
    void onMenuLogClicked();

    void onCameraInitialized(int cameraId, bool success);
    void reinitCameras();
    void onTimeTabClicked(int id);
    
    // Light Controller
    void connectLightController();
    void disconnectLightController();
    void setLightControllerLineCount(int lineCount);

    void onDefectTagClicked();
    void onRecipeChanged(const QString& recipeName);

    void updateDefectTags();

private:
    void setupUi();
    void setupLightTheme();
    void createNewBatch();

    // UI Panels
    QWidget* createLeftPanel();
    QWidget* createRightPanel();

    QList<CameraThread*> m_cameraThreads;
    int m_camerasReadyCount = 0;

    std::vector<InferThread*> m_inferThreads;

    ImageWidget *m_imageWidgetLeft = nullptr;
    ImageWidget* m_imageWidgetRight;
    ImageWidget* m_imageWidgetCenter;
    PieChartWidget* m_pieChart;

    SystemStatus m_currentStatus = Status_Stopped;
    // UI Elements
    QPushButton* m_btnStart;
    QPushButton* m_btnStop;
    QPushButton* m_btnConfig;
    QPushButton* m_btnDetect;
    QPushButton* m_btnLog;
    QMap<QString, QPushButton*> m_tagButtons;
    QLabel* m_statusTitle;
    QLabel* m_statusLabel;
    QLabel* m_lblPieceCount;
    QLabel* m_lblStatus;
    QLabel* m_lblTotal;
    
    // Light Controller
    QTcpSocket* m_lightCtrlSocket;
    QTimer* m_lightCtrlReconnectTimer;

    int m_totalCount;
    int m_ngCount;
    
    struct PendingFrameData {
        std::shared_ptr<ImageFrame> frame;
        cv::Mat imgBL;
        cv::Mat imgDF;
    };
    QMap<int, QMap<int, PendingFrameData>> m_pendingFrames;
    QMap<int, QMap<int, std::shared_ptr<ImageFrame>>> m_rawFrames;
    QSet<int> m_ngFrameIndices;
    int m_currentFrameIndex;
    QString m_currentBatchDir;
    QString m_runningRecipeName;
    
    // Store hidden defects
    QStringList m_hiddenDefects;
};

#endif // MAINWINDOW_H
