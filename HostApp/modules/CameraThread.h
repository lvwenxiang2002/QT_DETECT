#ifndef CAMERATHREAD_H
#define CAMERATHREAD_H

#include <QThread>
#include <QMutex>
#include <memory>
#include "../Common.h"

#include <windows.h>
#include "IKapC.h"
#include "IKapBoard.h"
#include <QTimer>

class CameraThread : public QThread
{
    Q_OBJECT
public:
    explicit CameraThread(int cameraId, QObject *parent = nullptr);
    ~CameraThread();

    void startCapture(bool isForRoi = false);
    void stopCapture();
    void resetFrameCounter() { m_frameIndex = 0; }
    void updateRoi(int offsetX, int offsetY, int width, int height);

signals:
    // 将采集到的图像发送给推理线程处理
    void imageCaptured(std::shared_ptr<ImageFrame> frame);
    void errorOccurred(QString msg);

public:
    void handleFrameReady();

protected:
    void run() override;

private:
    int m_cameraId;
    QMutex m_mutex;
    std::atomic<bool> m_isRunning;
    std::atomic<bool> m_isNextFrameForRoi;
    
    // I-TEK SDK Handles
    ITKDEVICE m_hCamera;
    HANDLE m_hBoard;
    
    int m_frameIndex;

    // 分块取图相关变量
    int m_expectedChunks = 1;
    int m_currentChunk = 0;
    std::vector<cv::Mat> m_chunkBuffers;
    int m_targetHeight = 0;
    int m_targetWidth = 0;

};

#endif // CAMERATHREAD_H
