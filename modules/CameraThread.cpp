#include "CameraThread.h"
#include <QDebug>
#include <QCoreApplication>
#include <QMetaObject>
#include "ConfigManager.h"
#include "RecipeManager.h"
#include <opencv2/opencv.hpp>

// 全局回调函数
void CALLBACK OnFrameReadyCallback(void* pContext)
{
    CameraThread* pThread = static_cast<CameraThread*>(pContext);
    if (pThread) {
        pThread->handleFrameReady();
    }
}

CameraThread::CameraThread(int cameraId, QObject *parent)
    : QThread(parent), m_cameraId(cameraId), m_isRunning(false), m_isReadyForTrigger(false), m_hCamera(NULL), m_hBoard(INVALID_HANDLE_VALUE), m_frameIndex(0)
{
    m_softTriggerTimer = new QTimer(this);
    m_softTriggerTimer->setSingleShot(true);
    connect(m_softTriggerTimer, &QTimer::timeout, this, &CameraThread::onSoftTrigger);
}

CameraThread::~CameraThread()
{
    stopCapture();
    quit();
    wait();
}

void CameraThread::startCapture(bool isForRoi)
{
    QMutexLocker locker(&m_mutex);
    if (isForRoi) m_isNextFrameForRoi = true;
    m_isRunning = true;
    m_softTriggerTimer->start(100);
}

void CameraThread::stopCapture()
{
    QMutexLocker locker(&m_mutex);
    m_isRunning = false;
    m_softTriggerTimer->stop();
}

static QMutex s_initMutex;

void CameraThread::run()
{
    qDebug() << "[CameraThread] Init Camera" << m_cameraId;

    ITKDEV_INFO di;
    int deviceIndexToOpen = -1;
    QString targetSN = "";
    
    {
        QMutexLocker locker(&s_initMutex);
        uint32_t numDevices = 0;
        ItkManGetDeviceCount(&numDevices);
    if (m_cameraId == 0) targetSN = ConfigManager::instance().camera.serialNumberLeft;
    else if (m_cameraId == 1) targetSN = ConfigManager::instance().camera.serialNumberRight;

    for (uint32_t i = 0; i < numDevices; ++i) {
        ITKDEV_INFO tempDi;
        ItkManGetDeviceInfo(i, &tempDi);
        QString sn = QString::fromLatin1(tempDi.SerialNumber);
        if (!targetSN.isEmpty() && sn == targetSN) {
            deviceIndexToOpen = i;
            di = tempDi;
            break;
        }
    }

    if (deviceIndexToOpen == -1 && targetSN.isEmpty() && (uint32_t)m_cameraId < numDevices) {
        deviceIndexToOpen = m_cameraId;
        ItkManGetDeviceInfo(deviceIndexToOpen, &di);
    }
    
    if (deviceIndexToOpen != -1) {
        ITKSTATUS res = ItkDevOpen(deviceIndexToOpen, ITKDEV_VAL_ACCESS_MODE_EXCLUSIVE, &m_hCamera);
        if (res != ITKSTATUS_OK) {
            QString errMsg = QString("打开相机 %1 失败!").arg(m_cameraId);
            qWarning() << "[CameraThread]" << errMsg;
            emit errorOccurred(errMsg);
            m_hCamera = NULL;
        }
    }
    } // unlock mutex

    if (deviceIndexToOpen == -1) {
        QString errMsg = QString("找不到相机 %1，目标 SN: %2").arg(m_cameraId).arg(targetSN);
        qWarning() << "[CameraThread]" << errMsg;
        emit errorOccurred(errMsg);
        return;
    }
    
    if (!m_hCamera) {
        return;
    }

    qDebug() << "[CameraThread] Found Camera:" << di.FullName << " Serial:" << di.SerialNumber << " Class:" << di.DeviceClass;
    
    // 2. 根据采集卡类型打开 Board
    ITK_GVB_DEV_INFO gvb_info = {};
    if (strcmp(di.DeviceClass, "GigEVisionBoard") == 0 || strcmp(di.DeviceClass, "GigEVision") == 0) {
        // 如果是 GigEVisionBoard
        ItkManGetGVBDeviceInfo(m_cameraId, &gvb_info);
        m_hBoard = IKapOpenWithSpecificInfo(&gvb_info);
    } else {
        // 其他类型暂不支持
        qWarning() << "[CameraThread] Unsupported Device Class:" << di.DeviceClass;
    }

    if (m_hBoard == INVALID_HANDLE_VALUE) {
        qWarning() << "[CameraThread] Failed to open grabber board!";
        if (m_hCamera) {
            ItkDevClose(m_hCamera);
            m_hCamera = NULL;
        }
        return;
    }

    // Apply settings from Recipe
    Recipe currentRecipe = RecipeManager::instance().getRecipe(RecipeManager::instance().getCurrentRecipeName());
    int rw = (m_cameraId == 0) ? currentRecipe.roiWidthLeft : currentRecipe.roiWidthRight;
    int rh = (m_cameraId == 0) ? currentRecipe.roiHeightLeft : currentRecipe.roiHeightRight;
    int rx = (m_cameraId == 0) ? currentRecipe.offsetXLeft : currentRecipe.offsetXRight;
    int ry = (m_cameraId == 0) ? currentRecipe.offsetYLeft : currentRecipe.offsetYRight;
    float expTime = (m_cameraId == 0) ? currentRecipe.exposureTimeLeft : currentRecipe.exposureTimeRight;
    
    ItkDevSetInt64(m_hCamera, "OffsetX", 0);
    ItkDevSetInt64(m_hCamera, "OffsetY", 0);
    ItkDevSetInt64(m_hCamera, "Width", rw);
    ItkDevSetInt64(m_hCamera, "Height", rh);
    ItkDevSetInt64(m_hCamera, "OffsetX", rx);
    ItkDevSetInt64(m_hCamera, "OffsetY", ry);
    ItkDevSetDouble(m_hCamera, "ExposureTime", expTime);

    // 3. 配置采集卡参数
    IKapSetInfo(m_hBoard, IKP_IMAGE_WIDTH, rw);
    IKapSetInfo(m_hBoard, IKP_IMAGE_HEIGHT, rh);
    IKapSetInfo(m_hBoard, IKP_FRAME_COUNT, 10);
    IKapSetInfo(m_hBoard, IKP_TIME_OUT, -1);
    IKapSetInfo(m_hBoard, IKP_GRAB_MODE, IKP_GRAB_NON_BLOCK);

    // 4. 设置相机软触发模式 (模拟采图)
    // 对于线阵相机，为了在没有编码器（行信号）和外触发（帧信号）的情况下采图，
    // 需要将行触发(LineStart)设为内部自由运行，并开启内部行频，
    // 将帧触发(FrameStart)设为软触发。
    
    // a. 设定行触发为内部
    ItkDevFromString(m_hCamera, "TriggerSelector", "LineStart");
    ItkDevFromString(m_hCamera, "TriggerMode", "Off");
    ItkDevSetDouble(m_hCamera, "AcquisitionLineRate", ConfigManager::instance().camera.lineRate); // 使用系统配置中的模拟行频

    // b. 设定帧触发为软触发
    ItkDevFromString(m_hCamera, "TriggerSelector", "FrameStart");
    ItkDevFromString(m_hCamera, "TriggerMode", "On");
    ItkDevFromString(m_hCamera, "TriggerSource", "Software");

    // 5. 注册采图回调
    IKapRegisterCallback(m_hBoard, IKEvent_FrameReady, (HookFnPtr)OnFrameReadyCallback, this);

    // 6. 开启抓图流
    IKapStartGrab(m_hBoard, 0);
    ItkDevExecuteCommand(m_hCamera, "AcquisitionStart");

    m_isReadyForTrigger = true;

    // 此时已经由主线程 startCapture 中调用了 m_softTriggerTimer->start(1500)
    
    // 进入事件循环，处理 QTimer 和回调
    exec();

    m_isReadyForTrigger = false;

    // 线程退出时的清理工作
    if (m_hCamera) {
        ItkDevExecuteCommand(m_hCamera, "AcquisitionStop");
    }
    if (m_hBoard != INVALID_HANDLE_VALUE) {
        IKapStopGrab(m_hBoard);
        IKapUnRegisterCallback(m_hBoard, IKEvent_FrameReady);
        IKapClose(m_hBoard);
        m_hBoard = INVALID_HANDLE_VALUE;
    }
    if (m_hCamera) {
        ItkDevClose(m_hCamera);
        m_hCamera = NULL;
    }
}

void CameraThread::onSoftTrigger()
{
    if (!m_isRunning) return;

    if (m_isReadyForTrigger && m_hCamera && m_hBoard != INVALID_HANDLE_VALUE) {
        ITKSTATUS res = ItkDevExecuteCommand(m_hCamera, "TriggerSoftware");
        qDebug() << "[CameraThread] Sending TriggerSoftware to camera" << m_cameraId << "res:" << res;
    } else {
        // 相机正在初始化中，尚未达到 AcquisitionStart 状态。100ms 后再次尝试触发！
        qDebug() << "[CameraThread] Camera" << m_cameraId << "not ready for trigger yet. Retrying in 100ms...";
        m_softTriggerTimer->start(100);
    }
}

void CameraThread::handleFrameReady()
{
    int nFrameIndex = 0;
    IKAPBUFFERSTATUS status;
    
    IKapGetInfo(m_hBoard, IKP_CURRENT_BUFFER_INDEX, &nFrameIndex);
    IKapGetBufferStatus(m_hBoard, nFrameIndex, &status);
    
    if (status.uFull == 1) {
        unsigned char* pUserBuffer = NULL;
        IKapGetBufferAddress(m_hBoard, nFrameIndex, (void**)&pUserBuffer);
        
        int64_t w = 0, h = 0;
        IKapGetInfo64(m_hBoard, IKP_IMAGE_WIDTH, &w);
        IKapGetInfo64(m_hBoard, IKP_IMAGE_HEIGHT, &h);
        
        if (pUserBuffer && w > 0 && h > 0) {
            // 直接将内存封装为 cv::Mat 并深拷贝 (因为底层 buffer 随时会被下一次采图覆盖)
            cv::Mat img(h, w, CV_8UC1, pUserBuffer);
            
            auto frame = std::make_shared<ImageFrame>();
            frame->cameraId = m_cameraId;
            frame->frameIndex = m_frameIndex++;
            frame->exposureType = 0; 
            
            if (m_isNextFrameForRoi.exchange(false)) {
                frame->isForRoi = true;
            }

            frame->image = img.clone(); 
            
            emit imageCaptured(frame);
        }
    }
    
    // 模拟真实的流水线间隔：当前帧拍摄完成（即玻璃走完全程）后，等待 500ms 再触发下一片玻璃
    if (m_isRunning) {
        QMetaObject::invokeMethod(m_softTriggerTimer, "start", Qt::QueuedConnection, Q_ARG(int, 500));
    }
}

void CameraThread::updateRoi(int offsetX, int offsetY, int width, int height) {
    if (!m_hCamera) return;
    
    // Always stop grab before changing resolution to prevent buffer mismatch
    ItkDevExecuteCommand(m_hCamera, "AcquisitionStop");
    if (m_hBoard != INVALID_HANDLE_VALUE) {
        IKapStopGrab(m_hBoard);
    }
    
    ITKSTATUS s1 = ItkDevSetInt64(m_hCamera, "OffsetX", 0);
    ITKSTATUS s2 = ItkDevSetInt64(m_hCamera, "OffsetY", 0);
    ITKSTATUS s3 = ItkDevSetInt64(m_hCamera, "Width", width);
    ITKSTATUS s4 = ItkDevSetInt64(m_hCamera, "Height", height);
    ITKSTATUS s5 = ItkDevSetInt64(m_hCamera, "OffsetX", offsetX);
    ITKSTATUS s6 = ItkDevSetInt64(m_hCamera, "OffsetY", offsetY);
    
    if (m_hBoard != INVALID_HANDLE_VALUE) {
        IKapSetInfo(m_hBoard, IKP_IMAGE_WIDTH, width);
        IKapSetInfo(m_hBoard, IKP_IMAGE_HEIGHT, height);
        IKapStartGrab(m_hBoard, 0);
    }
    ItkDevExecuteCommand(m_hCamera, "AcquisitionStart");

    qDebug() << "[CameraThread] ROI updated to" << offsetX << offsetY << width << height 
             << "Status:" << s1 << s2 << s3 << s4 << s5 << s6;
}

void CameraThread::updateExposure(float exposureTime) {
    if (!m_hCamera) return;
    
    ItkDevExecuteCommand(m_hCamera, "AcquisitionStop");
    if (m_hBoard != INVALID_HANDLE_VALUE) {
        IKapStopGrab(m_hBoard);
    }
    
    ITKSTATUS s = ItkDevSetDouble(m_hCamera, "ExposureTime", exposureTime);
    
    if (m_hBoard != INVALID_HANDLE_VALUE) {
        IKapStartGrab(m_hBoard, 0);
    }
    ItkDevExecuteCommand(m_hCamera, "AcquisitionStart");

    qDebug() << "[CameraThread] ExposureTime updated to" << exposureTime << "Status:" << s;
}
