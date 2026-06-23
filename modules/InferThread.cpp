#include "InferThread.h"
#include "InferKernels.h"
#include "RecipeManager.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <fstream>
#include <iostream>

using namespace nvinfer1;

void TRTLogger::log(Severity severity, const char* msg) noexcept {
    if (severity <= Severity::kWARNING) {
        qWarning() << "[TRT] " << msg;
    }
}

InferThread::InferThread(QObject *parent)
    : QThread(parent), m_isRunning(true)
{
    cudaStreamCreate(&m_stream);
    initMemoryPool();
}

InferThread::~InferThread()
{
    stop();
    releaseMemoryPool();
    cudaStreamDestroy(m_stream);
}

void InferThread::initMemoryPool()
{
    // 支持最大的单通道交错图像尺寸 16384 * 50000 (支持双分时最大 16K * 50K 图像)
    size_t max_image_size = 16384ULL * 50000ULL * sizeof(uint8_t);
    cudaHostAlloc((void**)&m_hImagePool, max_image_size, cudaHostAllocDefault);
    cudaMalloc((void**)&m_dImageBuffer, max_image_size);

    // TRT 输入 1 * 3 * 1280 * 1280 float (支持 1 或 3 通道)
    cudaMalloc((void**)&m_dInputBuffer, 1 * 3 * 1280 * 1280 * sizeof(float));

    // TRT 输出假设: YOLOv11 1280 尺寸，通常输出维度为 [1, 12, 33600] (4 个坐标 + 8 个类别)
    // 分配稍微充裕一些的内存，最大按 1 * 32 * 33600 float 算
    size_t out_size = 1 * 32 * 33600 * sizeof(float);
    cudaMalloc((void**)&m_dOutputBuffer, out_size);
    cudaHostAlloc((void**)&m_hOutputBuffer, out_size, cudaHostAllocDefault);
}

void InferThread::releaseMemoryPool()
{
    if (m_hImagePool) cudaFreeHost(m_hImagePool);
    if (m_dImageBuffer) cudaFree(m_dImageBuffer);
    if (m_dInputBuffer) cudaFree(m_dInputBuffer);
    if (m_dOutputBuffer) cudaFree(m_dOutputBuffer);
    if (m_hOutputBuffer) cudaFreeHost(m_hOutputBuffer);
}

bool InferThread::initializeTRT(const QString& engineBacklightPath, const QString& engineDarkfieldPath)
{
    m_runtime = createInferRuntime(m_logger);
    
    // Load Backlight Engine
    std::ifstream fileB(engineBacklightPath.toStdString(), std::ios::binary);
    if (!fileB.good()) {
        emit errorOccurred("Failed to read Backlight engine file");
        return false;
    }
    fileB.seekg(0, std::ios::end);
    size_t sizeB = fileB.tellg();
    fileB.seekg(0, std::ios::beg);
    std::vector<char> trtModelB(sizeB);
    fileB.read(trtModelB.data(), sizeB);
    fileB.close();
    
    m_engineBacklight = m_runtime->deserializeCudaEngine(trtModelB.data(), sizeB);
    if (!m_engineBacklight) return false;
    m_contextBacklight = m_engineBacklight->createExecutionContext();

    // Load Darkfield Engine
    std::ifstream fileD(engineDarkfieldPath.toStdString(), std::ios::binary);
    if (!fileD.good()) {
        emit errorOccurred("Failed to read Darkfield engine file");
        return false;
    }
    fileD.seekg(0, std::ios::end);
    size_t sizeD = fileD.tellg();
    fileD.seekg(0, std::ios::beg);
    std::vector<char> trtModelD(sizeD);
    fileD.read(trtModelD.data(), sizeD);
    fileD.close();
    
    m_engineDarkfield = m_runtime->deserializeCudaEngine(trtModelD.data(), sizeD);
    if (!m_engineDarkfield) return false;
    m_contextDarkfield = m_engineDarkfield->createExecutionContext();

    return m_contextBacklight != nullptr && m_contextDarkfield != nullptr;
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

void InferThread::pushImage(std::shared_ptr<ImageFrame> frame)
{
    QMutexLocker locker(&m_mutex);
    if (m_queue.size() > 5) {
        qWarning() << "Infer queue full, dropping frame.";
        return;
    }
    m_queue.enqueue(frame);
    m_condition.wakeOne();
}

void InferThread::processFrame(std::shared_ptr<ImageFrame> frame)
{
    if (!frame || frame->image.empty()) return;

    // 1. 将图像数据通过 Async 拷贝至 Device (假设 frame->image 已经是 Pinned Memory，或者走这里拷贝)
    size_t img_size = frame->image.total() * frame->image.elemSize();
    cudaMemcpyAsync(m_dImageBuffer, frame->image.data, img_size, cudaMemcpyHostToDevice, m_stream);

    int src_w = frame->image.cols;
    int src_h = frame->image.rows;
    int split_h = src_h / 2;

    int crop_size = 1280;
    int overlap = 20;
    int step = crop_size - overlap;

    // 分别处理两个光场
    for (int lf = 0; lf < 2; ++lf) {
        LightField current_lf = (lf == 0) ? LightField::Backlight : LightField::DarkField;

        // 遍历所有切图
        for (int y = 0; y < split_h; y += step) {
            for (int x = 0; x < src_w; x += step) {
                nvinfer1::IExecutionContext* current_context = (lf == 0) ? m_contextBacklight : m_contextDarkfield;
                nvinfer1::ICudaEngine* current_engine = (lf == 0) ? m_engineBacklight : m_engineDarkfield;
                if (current_context && current_engine) {
                    int channels = 1; // Default to 1

                    // 动态解析所有输入并填充具体尺寸
                    int nbBindings = current_engine->getNbBindings();
                    for (int i = 0; i < nbBindings; ++i) {
                        if (current_engine->bindingIsInput(i)) {
                            nvinfer1::Dims dims = current_engine->getBindingDimensions(i);
                            for (int j = 0; j < dims.nbDims; ++j) {
                                if (dims.d[j] == -1) {
                                    if (j == 0) dims.d[j] = 1;         // batch size
                                    else if (j == 1) dims.d[j] = 1;    // channels
                                    else dims.d[j] = 1280;             // spatial width/height
                                }
                            }
                            if (dims.nbDims >= 4) {
                                channels = dims.d[1]; // Use the explicitly defined or resolved channels
                            }
                            current_context->setBindingDimensions(i, dims);
                        }
                    }

                    // 2. 调用极致融合 Kernel
                    hpc_vision::launch_fused_preprocess_kernel(
                        m_dImageBuffer, src_w, src_h,
                        x, y, crop_size, overlap,
                        m_dInputBuffer, 1280,
                        lf, m_stream,
                        channels
                    );

                    void* bindings[] = {m_dInputBuffer, m_dOutputBuffer};
                    current_context->enqueueV2(bindings, m_stream, nullptr);

                    // 4. 将结果异步拷回 Host
                    // 动态查找输出 Binding 的索引
                    int out_idx = 1;
                    for (int i = 0; i < nbBindings; ++i) {
                        if (!current_engine->bindingIsInput(i)) {
                            out_idx = i;
                            break;
                        }
                    }

                    nvinfer1::Dims out_dims = current_context->getBindingDimensions(out_idx);
                    int out_size = 1;
                    for (int i = 0; i < out_dims.nbDims; ++i) {
                        out_size *= out_dims.d[i];
                    }
                    
                    cudaMemcpyAsync(m_hOutputBuffer, m_dOutputBuffer, out_size * sizeof(float), cudaMemcpyDeviceToHost, m_stream);
                    cudaStreamSynchronize(m_stream); // 等待结果

                    // 5. 后处理与坐标映射
                    // 传入获取到的 out_dims 方便后续处理
                    postProcess(frame, m_hOutputBuffer, out_dims, x, y, current_lf);
                }
            }
        }
    }
}

void InferThread::postProcess(std::shared_ptr<ImageFrame> frame, float* h_out, nvinfer1::Dims out_dims, int crop_x, int crop_y, LightField lf)
{
    // 动态解析 YOLO 维度: out_dims 通常是 [1, 12, 33600] 或 [1, 33600, 12]
    int num_anchors = 33600;
    int num_classes = 8;
    bool is_channels_first = true;

    if (out_dims.nbDims >= 3) {
        if (out_dims.d[1] < out_dims.d[2]) {
            // [1, 12, 33600]
            is_channels_first = true;
            num_classes = out_dims.d[1] - 4;
            num_anchors = out_dims.d[2];
        } else {
            // [1, 33600, 12]
            is_channels_first = false;
            num_anchors = out_dims.d[1];
            num_classes = out_dims.d[2] - 4;
        }
    }

    // 默认 NMS 阈值
    const float nms_threshold = 0.45f;

    // 读取 Recipe 参数
    QString recipeName = RecipeManager::instance().getCurrentRecipeName();
    Recipe recipe = RecipeManager::instance().getRecipe(recipeName);
    QString lfName = (lf == LightField::DarkField) ? "暗场" : "背光";
    LightFieldParam lfParam = recipe.lightFields.value(lfName);
    
    QStringList defectNames = {
        QString::fromUtf8("崩边"),
        QString::fromUtf8("划伤"),
        QString::fromUtf8("黑划伤"),
        QString::fromUtf8("凹凸点"),
        QString::fromUtf8("不可擦拭脏污"),
        QString::fromUtf8("点伤"),
        QString::fromUtf8("异色")
    };

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    // 解析 Tensor
    float global_max_conf = 0.0f;
    for (int i = 0; i < num_anchors; ++i) {
        float cx, cy, w, h;
        float max_conf = 0.0f;
        int best_class = -1;

        if (is_channels_first) {
            cx = h_out[0 * num_anchors + i];
            cy = h_out[1 * num_anchors + i];
            w  = h_out[2 * num_anchors + i];
            h  = h_out[3 * num_anchors + i];
            
            for (int c = 0; c < num_classes; ++c) {
                float conf = h_out[(4 + c) * num_anchors + i];
                if (conf > max_conf) {
                    max_conf = conf;
                    best_class = c;
                }
            }
        } else {
            cx = h_out[i * (num_classes + 4) + 0];
            cy = h_out[i * (num_classes + 4) + 1];
            w  = h_out[i * (num_classes + 4) + 2];
            h  = h_out[i * (num_classes + 4) + 3];
            
            for (int c = 0; c < num_classes; ++c) {
                float conf = h_out[i * (num_classes + 4) + 4 + c];
                if (conf > max_conf) {
                    max_conf = conf;
                    best_class = c;
                }
            }
        }
        
        if (max_conf > global_max_conf) {
            global_max_conf = max_conf;
        }

        // Apply Recipe Rules
        if (best_class >= 0 && best_class < defectNames.size()) {
            bool confOk = (max_conf >= recipe.globalInferConfidence);
            
            if (confOk) {
                // 转换为左上角坐标 (在 1280x1280 坐标系下)
                float x1 = cx - w / 2.0f;
                float y1 = cy - h / 2.0f;
                
                // 推理库侧只卡置信度（通过 confOk），面积和对角线等上位机参数在后续判定，仅用于区分显示大小
                boxes.push_back(cv::Rect(x1, y1, w, h));
                confidences.push_back(max_conf);
                class_ids.push_back(best_class);
            }
        }
    }

    // 获取当前动态 conf_threshold 最小值用于局部 NMS
    float dynamic_conf_threshold = 0.0f; 

    // 执行局部 NMS
    // YOLO 标准的分类别 NMS (Class-specific NMS)
    // 通过给不同类别的检测框加上巨大的偏移量，使得不同类别的框在物理上不可能相交，从而实现分类别独立的 NMS。
    std::vector<cv::Rect> offset_boxes;
    const int NMS_OFFSET = 4096; // 必须大于 crop_size (1280)
    for (size_t i = 0; i < boxes.size(); i++) {
        cv::Rect r = boxes[i];
        r.x += class_ids[i] * NMS_OFFSET;
        r.y += class_ids[i] * NMS_OFFSET;
        offset_boxes.push_back(r);
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(offset_boxes, confidences, dynamic_conf_threshold, nms_threshold, indices);

    // 坐标还原比例 (1280 -> 1280)
    float scale = 1.0f;

    // ---- Debug: print recipe used and filter results ----
    QString recipeName_debug = RecipeManager::instance().getCurrentRecipeName();
    qDebug() << "[postProcess] Recipe:" << recipeName_debug
             << "| LF:" << lfName
             << "| max_conf:" << global_max_conf
             << "| candidates_before_NMS:" << boxes.size()
             << "| after_NMS:" << indices.size();

    // Debug first high-conf anchor
    {
        float best_found_conf = 0.0f;
        int best_found_class = -1;
        float best_w = 0, best_h = 0;
        for (int i = 0; i < num_anchors; ++i) {
            float cx2, cy2, w2, h2;
            float mc = 0.0f;
            int bc = -1;
            if (is_channels_first) {
                cx2 = h_out[0 * num_anchors + i];
                cy2 = h_out[1 * num_anchors + i];
                w2  = h_out[2 * num_anchors + i];
                h2  = h_out[3 * num_anchors + i];
                for (int c = 0; c < num_classes; ++c) {
                    float cf = h_out[(4 + c) * num_anchors + i];
                    if (cf > mc) { mc = cf; bc = c; }
                }
            }
            if (mc > best_found_conf) {
                best_found_conf = mc; best_found_class = bc;
                best_w = w2; best_h = h2;
            }
        }
        if (best_found_class >= 0 && best_found_class < defectNames.size()) {
            QString dn = defectNames[best_found_class];
            DefectParam dp2 = lfParam.defectParams.value(dn, DefectParam());
            const float PIXEL_TO_MM = 0.039f;
            float area_mm2 = best_w * best_h * PIXEL_TO_MM * PIXEL_TO_MM;
            float diag_mm  = std::sqrt(best_w * best_w + best_h * best_h) * PIXEL_TO_MM;
            qDebug() << "[postProcess] Best anchor -> class:" << dn
                     << "conf:" << best_found_conf
                     << "w_px:" << best_w << "h_px:" << best_h
                     << "area_mm2:" << area_mm2 << "diag_mm:" << diag_mm;
            qDebug() << "[postProcess] Recipe filter -> minConf:" << dp2.minConfidence
                     << "maxConf:" << dp2.maxConfidence
                     << "minArea(mm2):" << dp2.minArea << "maxArea(mm2):" << dp2.maxArea
                     << "minDiag(mm):" << dp2.minDiagonal << "maxDiag(mm):" << dp2.maxDiagonal;
            bool confOk2 = (best_found_conf >= dp2.minConfidence) && (dp2.maxConfidence <= 0.001 || best_found_conf <= dp2.maxConfidence);
            bool areaOk2 = (area_mm2 >= dp2.minArea) && (dp2.maxArea < 0.001 || area_mm2 <= dp2.maxArea);
            bool diagOk2 = (diag_mm >= dp2.minDiagonal) && (dp2.maxDiagonal < 0.001 || diag_mm <= dp2.maxDiagonal);
            qDebug() << "[postProcess] confOk:" << confOk2 << "areaOk:" << areaOk2 << "diagOk:" << diagOk2;
        }
    }

    // 保存结果
    for (int idx : indices) {
        cv::Rect box = boxes[idx];
        
        // 映射回 2048 小图
        float scaled_x1 = box.x * scale;
        float scaled_y1 = box.y * scale;
        float scaled_w  = box.width * scale;
        float scaled_h  = box.height * scale;

        // 映射回原始大图 (基于 crop_x, crop_y 偏移)
        float abs_x1 = crop_x + scaled_x1;
        float abs_y1 = crop_y + scaled_y1;
        float abs_x2 = abs_x1 + scaled_w;
        float abs_y2 = abs_y1 + scaled_h;

        bool passedHostAppFilter = false;
        if (class_ids[idx] >= 0 && class_ids[idx] < defectNames.size()) {
            QString dn = defectNames[class_ids[idx]];
            DefectParam dp = lfParam.defectParams.value(dn, DefectParam());
            float PIXEL_TO_MM = 0.039f;
            float area_mm2 = scaled_w * scaled_h * PIXEL_TO_MM * PIXEL_TO_MM;
            float diag_mm = std::sqrt(scaled_w * scaled_w + scaled_h * scaled_h) * PIXEL_TO_MM;
            bool confOk = (confidences[idx] >= dp.minConfidence) && (dp.maxConfidence <= 0.001 || confidences[idx] <= dp.maxConfidence);
            bool areaOk = (area_mm2 >= dp.minArea) && (dp.maxArea < 0.001 || area_mm2 <= dp.maxArea);
            bool diagOk = (diag_mm >= dp.minDiagonal) && (dp.maxDiagonal < 0.001 || diag_mm <= dp.maxDiagonal);
            passedHostAppFilter = confOk && areaOk && diagOk;
        }

        DefectResult res;
        res.class_id = class_ids[idx];
        res.confidence = confidences[idx];
        res.x1 = abs_x1;
        res.y1 = abs_y1;
        res.x2 = abs_x2;
        res.y2 = abs_y2;
        res.area = scaled_w * scaled_h;
        res.diagonal = std::sqrt(scaled_w * scaled_w + scaled_h * scaled_h);
        res.light_type = lf;
        res.passedHostAppFilter = passedHostAppFilter;

        frame->results.push_back(res);
    }
}

void InferThread::run()
{
    // ==========================================
    // 模型路径配置 (Model Path Configuration)
    // 动态获取可执行文件所在目录，以便项目自由移动
    // ==========================================
    QString appDir = QCoreApplication::applicationDirPath();
    QString enginePathBL = appDir + "/../../test_models/1_1_1_tilt_light_defects_0616.engine";
    QString enginePathDF = appDir + "/../../test_models/tilt_dark.engine";
    
    if (!initializeTRT(enginePathBL, enginePathDF)) {
        qWarning() << "Failed to initialize TensorRT engines";
        emit errorOccurred("TRT Engine 加载失败，请检查路径: " + enginePathBL + " 和 " + enginePathDF);
    } else {
        qInfo() << "TensorRT Engines loaded successfully!";
    }

    while (true) {
        std::shared_ptr<ImageFrame> frame;
        {
            QMutexLocker locker(&m_mutex);
            while (m_queue.isEmpty() && m_isRunning) {
                m_condition.wait(&m_mutex);
            }
            if (!m_isRunning) break;
            frame = m_queue.dequeue();
        }
        QElapsedTimer timer;
        timer.start();
        processFrame(frame);
        frame->processTimeMs = timer.elapsed();
        
        qDebug() << "-----------------------------------------";
        qDebug() << "[InferThread] Camera ID:" << frame->cameraId 
                 << "Processed in" << frame->processTimeMs << "ms. "
                 << "Total Defects Found:" << frame->results.size();
        
        for (size_t i = 0; i < frame->results.size(); ++i) {
            const auto& res = frame->results[i];
            QString lightStr = (res.light_type == LightField::Backlight) ? "Backlight" : "Darkfield";
            qDebug() << "  -> Defect" << i+1 
                     << "| Light:" << lightStr 
                     << "| ClassID:" << res.class_id 
                     << "| Conf:" << QString::number(res.confidence, 'f', 2) 
                     << "| Rect: [" << res.x1 << "," << res.y1 << "," << res.x2 << "," << res.y2 << "]";
        }
        qDebug() << "-----------------------------------------";
        
        emit inferenceCompleted(frame);
    }
}
