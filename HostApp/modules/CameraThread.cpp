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
    : QThread(parent), m_cameraId(cameraId), m_isRunning(false), m_hCamera(NULL), m_hBoard(INVALID_HANDLE_VALUE), m_frameIndex(0)
{
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
    if (!isRunning()) {
        start();
    }
}

void CameraThread::stopCapture()
{
    QMutexLocker locker(&m_mutex);
    m_isRunning = false;
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

    // --- [NEW] Load CCF file ---
    QString ccfPath = ConfigManager::instance().camera.ccfFilePath;
    if (!ccfPath.isEmpty() && QFile::exists(ccfPath)) {
        ITKSTATUS loadRes = ItkDevLoadConfigurationFromFile(m_hCamera, (char*)ccfPath.toLocal8Bit().data());
        qDebug() << "[CameraThread] Loaded CCF file:" << ccfPath << "Result:" << loadRes;
    }

    // Apply settings from Recipe
    Recipe currentRecipe = RecipeManager::instance().getRecipe(RecipeManager::instance().getCurrentRecipeName());
    int rw = (m_cameraId == 0) ? currentRecipe.roiWidthLeft : currentRecipe.roiWidthRight;
    // 不定行高模式：物理 Buffer 给到一个安全最大值，实际高度由硬件 VFL 决定
    int maxVflHeight = 120000;
    int rx = (m_cameraId == 0) ? currentRecipe.offsetXLeft : currentRecipe.offsetXRight;
    int ry = (m_cameraId == 0) ? currentRecipe.offsetYLeft : currentRecipe.offsetYRight;
    
    m_targetWidth = rw;
    m_targetHeight = maxVflHeight;
    m_expectedChunks = 1;
    int m_chunkHeight = maxVflHeight;
    
    // 按照您的要求，即使软触发也强制一镜到底，取消所有切块逻辑
    // 如果几万行在测试时导致 IKapStartGrab failed，请自行修改系统注册表扩大 Non-Paged Pool
    
    ItkDevSetInt64(m_hCamera, "OffsetX", 0);
    ItkDevSetInt64(m_hCamera, "OffsetY", 0);
    ItkDevSetInt64(m_hCamera, "Width", m_targetWidth);
    ItkDevSetInt64(m_hCamera, "Height", m_chunkHeight);
    ItkDevSetInt64(m_hCamera, "OffsetX", rx);
    ItkDevSetInt64(m_hCamera, "OffsetY", ry);

    // 3. 配置采集卡参数
    int s1 = IKapSetInfo(m_hBoard, IKP_IMAGE_WIDTH, m_targetWidth);
    int s2 = IKapSetInfo(m_hBoard, IKP_IMAGE_HEIGHT, m_chunkHeight);
    int s3 = IKapSetInfo(m_hBoard, IKP_FRAME_COUNT, 3); // 强制申请3帧缓冲，哪怕尺寸超大也交由注册表去处理 Non-Paged Pool
    int s4 = IKapSetInfo(m_hBoard, IKP_TIME_OUT, -1);
    int s5 = IKapSetInfo(m_hBoard, IKP_GRAB_MODE, IKP_GRAB_NON_BLOCK);
    
    qDebug() << "[CameraThread] IKapSetInfo Init Setup:" << s1 << s2 << s3 << s4 << s5;

    // 4. 不再主动设置触发模式，完全依赖相机的初始硬件设置或外部配置软件


    // 5. 注册采图回调
    IKapRegisterCallback(m_hBoard, IKEvent_FrameReady, (HookFnPtr)OnFrameReadyCallback, this);

    // 6. 开启抓图流
    int startGrabRes = IKapStartGrab(m_hBoard, 0);
    if (startGrabRes == 0) { // 0 in IKap SDK means IK_RTN_ERROR
        qWarning() << "[CameraThread] CRITICAL ERROR: IKapStartGrab failed! This usually means the OS Non-Paged Pool (DMA memory) rejected the massive buffer allocation (e.g. 800MB). Please reduce ROI Height.";
    }
    
    ITKSTATUS startRes = ItkDevExecuteCommand(m_hCamera, "AcquisitionStart");
    qDebug() << "[CameraThread] IKapStartGrab res:" << startGrabRes << "AcquisitionStart res:" << startRes;

    // Emit initialized signal
    emit cameraInitialized(m_cameraId, true);

    // 此时已经由主线程 startCapture 中调用了 m_softTriggerTimer->start(1500)
    
    // 进入事件循环，处理 QTimer 和回调
    exec();

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



void CameraThread::handleFrameReady()
{
    if (!m_isRunning && !m_isNextFrameForRoi) {
        return;
    }

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
            // VFL 模式下，实际采到的有效行数在 status.uLineNum
            int actualHeight = status.uLineNum;
            if (actualHeight <= 0 || actualHeight > h) {
                actualHeight = h; // Fallback 如果硬件未正确返回有效行，则使用最大值
            }

            cv::Mat rawImg(actualHeight, w, CV_8UC1, pUserBuffer);
            // --- 基于首行亮度的光场纠偏算法 (用户要求回复首行判断) ---
            long long sumRow0 = 0;
            long long sumRow1 = 0;
            int samples = 0;
            
            // 直接对比第0行和第1行的全局平均亮度
            const uchar* pRow0 = rawImg.ptr<uchar>(0);
            const uchar* pRow1 = rawImg.ptr<uchar>(1);
            int stepX = (std::max)(1, (int)w / 1000); // 采样1000个点足够快
            for (int c = 0; c < w; c += stepX) {
                sumRow0 += pRow0[c];
                sumRow1 += pRow1[c];
                samples++;
            }
            
            double avgRow0 = samples > 0 ? (double)sumRow0 / samples : 0;
            double avgRow1 = samples > 0 ? (double)sumRow1 / samples : 0;
            
            int startRow = 0;
            // 正常情况下，明场(第0行)的亮度远大于暗场(第1行)
            // 如果第0行平均亮度低于第1行（加个10的容差防噪），说明首行是暗场，发生了反相
            if (avgRow0 + 10.0 < avgRow1) {
                startRow = 1;
                actualHeight -= 1;
                qDebug() << "[CameraThread] Camera" << m_cameraId << "Light field swapped! Auto-correcting. BL(Row1):" << avgRow1 << "DF(Row0):" << avgRow0;
            }

            // 强制将高度对齐为偶数
            if (actualHeight % 2 != 0) {
                actualHeight -= 1;
            }

            cv::Mat fullImg = rawImg(cv::Rect(0, startRow, w, actualHeight)).clone();

            if (fullImg.rows > 0) {
                const uchar* lastRow = fullImg.ptr<uchar>(fullImg.rows - 1);
                int midX = fullImg.cols / 2;
                QString pixelStr = "";
                for (int c = 0; c < 10; ++c) {
                    pixelStr += QString::number(lastRow[midX + c]) + " ";
                }
                // qDebug() << "[CameraThread] Last Row Pixels (Middle 10):" << pixelStr;
            }
            
            auto frame = std::make_shared<ImageFrame>();
            frame->cameraId = m_cameraId;
            frame->frameIndex = m_frameIndex++;
            frame->exposureType = 0; 
            
            if (m_isNextFrameForRoi.exchange(false)) {
                frame->isForRoi = true;
            }

            frame->image = fullImg; 
            
            emit imageCaptured(frame);
            // qDebug() << "[CameraThread] Camera" << m_cameraId << "successfully captured VFL frame" << (m_frameIndex - 1)
            //          << "Final Size:" << fullImg.cols << "x" << fullImg.rows;
        } else {
            qWarning() << "[CameraThread] Camera" << m_cameraId << "capture failed! Invalid dimensions or buffer. w:" << w << "h:" << h << "pUserBuffer:" << pUserBuffer;
        }
    } else {
        qWarning() << "[CameraThread] Camera" << m_cameraId << "FrameReady event triggered but buffer status uFull is not 1! status.uFull:" << status.uFull;
    }
}

void CameraThread::updateRoi(int offsetX, int offsetY, int width) {
    if (!m_hCamera) return;
    
    // Always stop grab before changing resolution to prevent buffer mismatch
    ItkDevExecuteCommand(m_hCamera, "AcquisitionStop");
    if (m_hBoard != INVALID_HANDLE_VALUE) {
        IKapStopGrab(m_hBoard);
    }
    
    m_targetWidth = width;
    // 不定行高模式：物理 Buffer 始终为安全最大值
    m_targetHeight = 120000;
    m_expectedChunks = 1;
    int m_chunkHeight = 120000;
    
    // 取消切块逻辑
    
    ITKSTATUS s1 = ItkDevSetInt64(m_hCamera, "OffsetX", 0);
    ITKSTATUS s2 = ItkDevSetInt64(m_hCamera, "OffsetY", 0);
    ITKSTATUS s3 = ItkDevSetInt64(m_hCamera, "Width", m_targetWidth);
    ITKSTATUS s4 = ItkDevSetInt64(m_hCamera, "Height", m_chunkHeight);
    ITKSTATUS s5 = ItkDevSetInt64(m_hCamera, "OffsetX", offsetX);
    ITKSTATUS s6 = ItkDevSetInt64(m_hCamera, "OffsetY", offsetY);
    
    if (m_hBoard != INVALID_HANDLE_VALUE) {
        int s7 = IKapSetInfo(m_hBoard, IKP_IMAGE_WIDTH, m_targetWidth);
        int s8 = IKapSetInfo(m_hBoard, IKP_IMAGE_HEIGHT, m_chunkHeight);
        IKapSetInfo(m_hBoard, IKP_FRAME_COUNT, 3);
        
        int s9 = IKapStartGrab(m_hBoard, 0);
        qDebug() << "[CameraThread] IKapSetInfo ROI Update res:" << s7 << s8 << "StartGrab:" << s9;
    }
    ITKSTATUS s10 = ItkDevExecuteCommand(m_hCamera, "AcquisitionStart");
    qDebug() << "[CameraThread] AcquisitionStart ROI Update res:" << s10;

    qDebug() << "[CameraThread] ROI updated to" << offsetX << offsetY << width
             << "Status:" << s1 << s2 << s3 << s4 << s5 << s6;
}
