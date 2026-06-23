#include "CameraThread.h"
#include <QDebug>
#include <QPainter>
#include <QCoreApplication>

#include "ConfigManager.h"
#include <QDir>
#include <QStringList>
CameraThread::CameraThread(int cameraId, QObject *parent)
    : QThread(parent), m_cameraId(cameraId), m_isRunning(false)
{
}

CameraThread::~CameraThread()
{
    stopCapture();
    wait();
}

void CameraThread::startCapture()
{
    QMutexLocker locker(&m_mutex);
    m_isRunning = true;
    if (!isRunning()) {
        start();
    }
}

void CameraThread::stopCapture()
{
    QMutexLocker locker(&m_mutex);
    m_isRunning = false;
}

void CameraThread::run()
{
    // TODO: 1. 初始化 MVS 相机
    // MV_CC_CreateHandle(&m_hCamera, ...);
    // MV_CC_OpenDevice(m_hCamera, ...);
    // 2. 针对双分时曝光设置参数：
    // MV_CC_SetEnumValue(m_hCamera, "ExposureMode", MV_EXPOSURE_MODE_TIMED);
    // MV_CC_StartGrabbing(m_hCamera);
    // MV_FRAME_OUT stImageInfo;
    
    QString appDir = QCoreApplication::applicationDirPath();
    QString camDir = appDir + "/../../test_images" + QString("/Camera%1").arg(m_cameraId);
    QDir dir(camDir);
    QStringList filters;
    filters << "*.bmp" << "*.jpg" << "*.png" << "*.tif";
    QStringList imageFiles = dir.entryList(filters, QDir::Files | QDir::NoSymLinks);
    if (imageFiles.isEmpty()) {
        qWarning() << "No simulation images found in" << camDir;
    } else {
        qDebug() << "Found" << imageFiles.size() << "simulation images in" << camDir;
    }
    
    int frameIndex = 0;
    int fileIndex = 0;
    
    while (true) {
        {
            QMutexLocker locker(&m_mutex);
            if (!m_isRunning) break;
        }
        
        std::shared_ptr<ImageFrame> frame = std::make_shared<ImageFrame>();
        frame->cameraId = m_cameraId;
        frame->frameIndex = frameIndex++;
        // The user specifies these are dual-time unsplit images.
        // exposureType can remain 0 since both fields are in one image.
        frame->exposureType = 0; 
        
        if (!imageFiles.isEmpty()) {
            QString filePath = dir.absoluteFilePath(imageFiles[fileIndex]);
            std::string pathStd = filePath.toLocal8Bit().constData();
            // Read image in grayscale as industrial images usually are
            frame->image = cv::imread(pathStd, cv::IMREAD_GRAYSCALE); 
            
            fileIndex = (fileIndex + 1) % imageFiles.size(); // Loop around
        }
        
        if (frame->image.empty()) {
            // Fallback to dummy generation if no files or failed to load
            frame->image = cv::Mat(50000, 16384, CV_8UC1);
            if (m_cameraId == 0) frame->image.setTo(cv::Scalar(40));
            else frame->image.setTo(cv::Scalar(80));
        }

        emit imageCaptured(frame);
        
        QThread::msleep(1500); // 1.5 seconds per frame for testing
    }
    
    // MV_CC_StopGrabbing(m_hCamera);
    // MV_CC_CloseDevice(m_hCamera);
    // MV_CC_DestroyHandle(m_hCamera);
}
