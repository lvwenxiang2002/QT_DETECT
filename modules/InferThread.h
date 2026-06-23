#ifndef INFERTHREAD_H
#define INFERTHREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <memory>
#include <vector>

#include <cuda_runtime.h>
#include <NvInfer.h>
#include "../Common.h"

// TensorRT Logger
class TRTLogger : public nvinfer1::ILogger {
    void log(Severity severity, const char* msg) noexcept override;
};

class InferThread : public QThread
{
    Q_OBJECT
public:
    explicit InferThread(QObject *parent = nullptr);
    ~InferThread();

    void stop();
    bool initializeTRT(const QString& engineBacklightPath, const QString& engineDarkfieldPath);
    
public slots:
    void pushImage(std::shared_ptr<ImageFrame> frame);

signals:
    void inferenceCompleted(std::shared_ptr<ImageFrame> frame);
    void errorOccurred(QString errorMsg);

protected:
    void run() override;

private:
    void initMemoryPool();
    void releaseMemoryPool();
    void processFrame(std::shared_ptr<ImageFrame> frame);
    void postProcess(std::shared_ptr<ImageFrame> frame, float* h_out, nvinfer1::Dims out_dims, int crop_x, int crop_y, LightField lf);

    bool m_isRunning;
    QMutex m_mutex;
    QWaitCondition m_condition;
    QQueue<std::shared_ptr<ImageFrame>> m_queue;

    // TensorRT
    TRTLogger m_logger;
    nvinfer1::IRuntime* m_runtime = nullptr;
    
    // 一个光场对应一个模型
    nvinfer1::ICudaEngine* m_engineBacklight = nullptr;
    nvinfer1::IExecutionContext* m_contextBacklight = nullptr;

    nvinfer1::ICudaEngine* m_engineDarkfield = nullptr;
    nvinfer1::IExecutionContext* m_contextDarkfield = nullptr;

    // CUDA & Memory Pool
    cudaStream_t m_stream;
    
    // 我们假设大图被拆解成 N 个 2048x2048 的 patch
    // 为了最大化吞吐，我们可以一次推一个 batch。这里为了简单，单 stream 循环 enqueue
    float* m_dInputBuffer = nullptr;  // TensorRT 输入 (1x1x1280x1280, device)
    float* m_dOutputBuffer = nullptr; // TensorRT 输出 (例如: 1xNumBoxesx6, device)
    float* m_hOutputBuffer = nullptr; // TensorRT 输出 (Host, Pinned)
    
    // 相机过来的大图如果还没有锁页，可以在这里预先分配 Pinned Memory 用于中转
    uint8_t* m_hImagePool = nullptr;
    uint8_t* m_dImageBuffer = nullptr; // 大图 Device 端缓冲
};

#endif // INFERTHREAD_H
