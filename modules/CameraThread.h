#ifndef CAMERATHREAD_H
#define CAMERATHREAD_H

#include <QThread>
#include <QMutex>
#include <memory>
#include "../Common.h"

// 模拟海康相机采集线程
class CameraThread : public QThread
{
    Q_OBJECT
public:
    explicit CameraThread(int cameraId, QObject *parent = nullptr);
    ~CameraThread();

    void startCapture();
    void stopCapture();

signals:
    // 将采集到的图像发送给推理线程处理
    void imageCaptured(std::shared_ptr<ImageFrame> frame);

protected:
    void run() override;

private:
    int m_cameraId;
    bool m_isRunning;
    QMutex m_mutex;
    
    // MVS SDK Handle here: 
    // void* m_hCamera;
};

#endif // CAMERATHREAD_H
