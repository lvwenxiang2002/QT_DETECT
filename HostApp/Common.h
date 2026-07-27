#ifndef COMMON_H
#define COMMON_H

#include <QString>
#include <memory>
#include <vector>
#include <QMetaType>
#include <opencv2/opencv.hpp>
#include <mutex>

enum class LightField {
    Backlight = 0,
    DarkField = 1
};

// 推理结果的包围盒结构
struct DefectResult {
    int class_id;          // 缺陷类别 (0-7: 白点、崩边、划伤、气泡、麻点、脏污、黑划伤、黑点)
    float confidence;      // 置信度
    float x1, y1, x2, y2;  // 原始大图上的绝对坐标(左上与右下)
    float area;            // 面积
    float diagonal;        // 对角线长度
    LightField light_type; // 光场标识符
    bool passedHostAppFilter; // 是否通过了上位机的所有过滤条件（决定绘制样式大小）
    int max_val = 0;       // 框内最高灰度值
    float mean_val = 0.0f; // 框内平均灰度值
};

// 图像帧结构体
struct ImageFrame {
    int cameraId;
    int frameIndex;
    qint64 captureTimestamp; // 用于绝对时间戳对齐
    bool isForRoi = false; // Flag to indicate if this frame is specifically for ROI setup and should bypass inference
    
    // 我们将使用 cv::Mat 指向 Pinned Memory，避免在高频操作中使用 QImage
    cv::Mat image; 
    
    // 海康双分时曝光：由于大图是奇偶行交错的单张大图，这里存储整图
    int exposureType; 
    
    // 标记当前帧是否是在双相机拼图模式下生成的
    bool isStitchingMode = false;

    // 推理完成后的结果存储在此处，最终会统一绘制在背光图像上
    std::vector<DefectResult> results; 
    
    // 被 Ghost Filter 杀死的框的日志，用于在最后汇总打印
    std::vector<QString> killedLogs;
    
    // 多线程后处理使用的结果写入锁
    std::mutex resultsMutex;
    
    // 耗时记录 (毫秒)
    int processTimeMs = 0;
};

// 注册智能指针类型，允许其在信号槽机制中跨线程传递
Q_DECLARE_METATYPE(std::shared_ptr<ImageFrame>)

#endif // COMMON_H
