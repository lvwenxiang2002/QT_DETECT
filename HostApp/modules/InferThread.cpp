#include "InferThread.h"
#include "RecipeManager.h"
#include "ConfigManager.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>

InferThread::InferThread(QObject *parent)
    : QThread(parent), m_isRunning(true), m_inferLib("InferLib.dll")
{
    if (m_inferLib.load()) {
        m_createFunc = (CreateEngineFunc)m_inferLib.resolve("createInferenceEngine");
        m_destroyFunc = (DestroyEngineFunc)m_inferLib.resolve("destroyInferenceEngine");
        
        if (m_createFunc && m_destroyFunc) {
            m_engine = m_createFunc();
            qDebug() << "[InferThread] Successfully loaded InferLib.dll and created engine.";
        } else {
            qWarning() << "[InferThread] Failed to resolve functions in InferLib.dll.";
        }
    } else {
        qWarning() << "[InferThread] Failed to load InferLib.dll:" << m_inferLib.errorString();
    }
}

InferThread::~InferThread()
{
    stop();
    if (m_engine && m_destroyFunc) {
        m_destroyFunc(m_engine);
        m_engine = nullptr;
    }
    
    if (m_inferLib.isLoaded()) {
        m_inferLib.unload();
    }
}

void InferThread::stop()
{
    {
        QMutexLocker locker(&m_mutex);
        m_isRunning = false;
        m_condition.wakeAll();
    }
    wait();
}

bool InferThread::initializeTRT(const QString& engineBacklightPath, const QString& engineDarkfieldPath)
{
    if (!m_engine) return false;
    
    QByteArray baBack = engineBacklightPath.toUtf8();
    QByteArray baDark = engineDarkfieldPath.toUtf8();
    
    return m_engine->init(baBack.constData(), baDark.constData(), 1);
}

void InferThread::pushImage(std::shared_ptr<ImageFrame> frame)
{
    QMutexLocker locker(&m_mutex);
    if (m_queue.size() > 5) {
        m_queue.dequeue();
        qWarning() << "[InferThread] Queue full, dropping oldest frame to save memory";
    }
    m_queue.enqueue(frame);
    m_condition.wakeOne();
}

void InferThread::run()
{
    while (m_isRunning) {
        std::shared_ptr<ImageFrame> frame;
        {
            QMutexLocker locker(&m_mutex);
            if (m_queue.isEmpty()) {
                m_condition.wait(&m_mutex);
                if (!m_isRunning) break;
            }
            if (!m_queue.isEmpty()) {
                frame = m_queue.dequeue();
            }
        }

        if (frame) {
            processFrame(frame);
        }
    }
}

void InferThread::processFrame(std::shared_ptr<ImageFrame> frame)
{
    QElapsedTimer timer;
    timer.start();

    if (!m_engine) {
        emit errorOccurred("Inference Engine not loaded");
        return;
    }

    if (frame->image.empty() || frame->image.type() != CV_8UC1) {
        emit errorOccurred("Invalid image format for inference");
        return;
    }

    // 1. 获取两组光场的 Recipe 参数，配置给 DLL
    QString recipeName = RecipeManager::instance().getCurrentRecipeName();
    Recipe recipe = RecipeManager::instance().getRecipe(recipeName);
    
    InferConfig configBack, configDark;
    configBack.globalInferConfidence = recipe.globalInferConfidence;
    configBack.nmsThreshold = 0.45f;
    configBack.cropSize = 1280;
    configBack.overlap = 20;

    configDark = configBack;

    // 2. 准备输出缓冲区 (最多支持 2000 个缺陷)
    const int MAX_RESULTS = 2000;
    std::vector<InferResult> results(MAX_RESULTS);
    int outCount = 0;

    // 3. 将完整大图抛给 DLL
    bool success = m_engine->process(
        frame->image.data, 
        frame->image.cols, 
        frame->image.rows, 
        configBack, configDark,
        results.data(), MAX_RESULTS, outCount
    );

    if (!success) {
        emit errorOccurred("DLL Inference execution failed");
        return;
    }

    // 4. 解析结果转换为 UI 用的 DefectResult
    frame->results.clear();
    bool hasNg = false;

    QStringList defectNames = {
        QString::fromUtf8("崩边"),
        QString::fromUtf8("划伤"),
        QString::fromUtf8("黑划伤"),
        QString::fromUtf8("凹凸点"),
        QString::fromUtf8("不可擦拭脏污"),
        QString::fromUtf8("点伤"),
        QString::fromUtf8("异色")
    };

    const float PIXEL_TO_MM = 0.039f;

    for (int i = 0; i < outCount; ++i) {
        const InferResult& res = results[i];
        
        QString lfName = (res.lightField == 0) ? "背光" : "暗场";
        LightFieldParam lfParam = recipe.lightFields.value(lfName);

        if (res.classId >= 0 && res.classId < defectNames.size()) {
            QString dn = defectNames[res.classId];
            DefectParam dp = lfParam.defectParams.value(dn, DefectParam());

            float area_mm2 = res.width * res.height * PIXEL_TO_MM * PIXEL_TO_MM;
            float diag_mm  = std::sqrt(res.width * res.width + res.height * res.height) * PIXEL_TO_MM;

            bool confOk = (res.confidence >= dp.minConfidence) && (dp.maxConfidence < 0.001 || res.confidence <= dp.maxConfidence);
            bool areaOk = (area_mm2 >= dp.minArea) && (dp.maxArea < 0.001 || area_mm2 <= dp.maxArea);
            bool diagOk = (diag_mm >= dp.minDiagonal) && (dp.maxDiagonal < 0.001 || diag_mm <= dp.maxDiagonal);

            if (confOk && areaOk && diagOk) {
                // 边界框物理溢出处理 (Clipping)
                int physical_w = frame->image.cols;
                int physical_h = frame->image.rows / 2; // 交错图像的一半高度是真实物理高度
                cv::Rect physicalRect(0, 0, physical_w, physical_h);
                cv::Rect defectRect(res.x, res.y, res.width, res.height);
                cv::Rect clipped = defectRect & physicalRect;
                
                if (clipped.area() <= 0) {
                    qDebug().noquote() << QString("[Defect FILTERED] %1-%2 | Clipped to zero (out of physical bounds)").arg(lfName).arg(dn);
                    continue; // 完全落入 Padding 区，直接抹杀
                }

                // Ghost Box Killer 像素级过滤
                const uint8_t* ptr = frame->image.ptr<uint8_t>();
                int stride = frame->image.step;
                int y_offset = (res.lightField == 0) ? 0 : 1;
                uint8_t min_v = 255, max_v = 0;
                long long sum = 0;
                int count = 0;
                
                for (int r = clipped.y; r < clipped.y + clipped.height; ++r) {
                    int src_r = r * 2 + y_offset;
                    if (src_r < frame->image.rows) {
                        const uint8_t* row_ptr = ptr + src_r * stride;
                        for (int c = clipped.x; c < clipped.x + clipped.width; ++c) {
                            if (c < frame->image.cols) {
                                uint8_t v = row_ptr[c];
                                if (v < min_v) min_v = v;
                                if (v > max_v) max_v = v;
                                sum += v;
                                count++;
                            }
                        }
                    }
                }
                
                double mean_v = count > 0 ? (double)sum / count : 0.0;
                auto& gCfg = ConfigManager::instance().general;
                bool isGhost = false;
                
                if (res.lightField == 1) { // 暗场
                    if (max_v <= gCfg.ghostMaxValThreshold || mean_v <= gCfg.ghostMeanValThreshold) {
                        isGhost = true;
                    }
                } else { // 背光
                    if ((255 - min_v) <= gCfg.ghostMaxValThreshold || (255 - mean_v) <= gCfg.ghostMeanValThreshold) {
                        isGhost = true;
                    }
                }
                
                if (isGhost) {
                    qDebug().noquote() << QString("[Defect FILTERED (Ghost Box)] %1-%2 | Conf: %3% | min:%4 max:%5 mean:%6")
                                            .arg(lfName).arg(dn).arg((int)(res.confidence * 100)).arg(min_v).arg(max_v).arg(mean_v, 0, 'f', 1);
                    continue;
                }

                DefectResult d;
                d.class_id = res.classId;
                d.confidence = res.confidence;
                d.x1 = clipped.x;
                d.y1 = clipped.y;
                d.x2 = clipped.x + clipped.width;
                d.y2 = clipped.y + clipped.height;
                d.area = area_mm2;
                d.diagonal = diag_mm;
                d.light_type = (res.lightField == 0) ? LightField::Backlight : LightField::DarkField;
                d.passedHostAppFilter = true;
                
                frame->results.push_back(d);
                hasNg = true;
                qDebug().noquote() << QString("[Defect DETECTED] %1-%2 | Conf: %3% | Area: %4 mm2 | Diag: %5 mm")
                                        .arg(lfName).arg(dn).arg((int)(res.confidence * 100))
                                        .arg(area_mm2, 0, 'f', 2).arg(diag_mm, 0, 'f', 2);
            } else {
                qDebug().noquote() << QString("[Defect FILTERED] %1-%2 | Conf: %3% (Allowed: %10~%11) | Area: %4 mm2 (Allowed: %5~%6) | Diag: %7 mm (Allowed: %8~%9)")
                                        .arg(lfName).arg(dn).arg((int)(res.confidence * 100))
                                        .arg(area_mm2, 0, 'f', 2).arg(dp.minArea, 0, 'f', 2).arg(dp.maxArea < 0.001 ? "INF" : QString::number(dp.maxArea, 'f', 2))
                                        .arg(diag_mm, 0, 'f', 2).arg(dp.minDiagonal, 0, 'f', 2).arg(dp.maxDiagonal < 0.001 ? "INF" : QString::number(dp.maxDiagonal, 'f', 2))
                                        .arg((int)(dp.minConfidence * 100)).arg(dp.maxConfidence < 0.001 ? "INF" : QString::number((int)(dp.maxConfidence * 100)));
            }
        }
    }

    qint64 elapsed = timer.elapsed();
    qDebug() << "[InferThread] DLL Inference completed in" << elapsed << "ms. Total valid defects:" << frame->results.size();

    frame->processTimeMs = elapsed; // 赋值给 frame 以便 UI 显示
    emit inferenceCompleted(frame);
}
