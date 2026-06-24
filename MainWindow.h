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
#include "widgets/ImageWidget.h"
#include "widgets/PieChartWidget.h"
#include "modules/CameraThread.h"
#include "modules/InferThread.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onStartClicked();
    void onStopClicked();
    void onImageProcessed(std::shared_ptr<ImageFrame> frame);

    // New slots for interaction
    void onMenuConfigClicked();
    void onMenuDetectClicked();
    void onMenuLogClicked();
    void onTimeTabClicked(int id);
    void onDefectTagClicked();
    void onRecipeChanged(const QString& recipeName);

    void updateDefectTags();

private:
    void setupUi();
    void setupLightTheme();

    // UI Panels
    QWidget* createLeftPanel();
    QWidget* createRightPanel();

    std::vector<CameraThread*> m_cameraThreads;
    std::vector<InferThread*> m_inferThreads;

    ImageWidget* m_imageWidgetLeft;
    ImageWidget* m_imageWidgetRight;
    PieChartWidget* m_pieChart;

    // UI Elements
    QPushButton* m_btnStart;
    QPushButton* m_btnStop;
    QPushButton* m_btnConfig;
    QPushButton* m_btnDetect;
    QPushButton* m_btnLog;
    QMap<QString, QPushButton*> m_tagButtons;
    QLabel* m_statusTitle;
    QLabel* m_lblPieceCount;
    QLabel* m_lblStatus;
    QLabel* m_lblTotal;
    
    int m_totalCount;
    int m_ngCount;
    
    struct PendingFrameData {
        std::shared_ptr<ImageFrame> frame;
        cv::Mat imgBL;
        cv::Mat imgDF;
    };
    QMap<int, QMap<int, PendingFrameData>> m_pendingFrames;
    QSet<int> m_ngFrameIndices;
    int m_currentFrameIndex;
    
    // Store hidden defects
    QStringList m_hiddenDefects;
};

#endif // MAINWINDOW_H
