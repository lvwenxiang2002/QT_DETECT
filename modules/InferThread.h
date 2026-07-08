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
    
    // 异步流水线 (Ring Buffer) 设置
    static const int NUM_BUFFERS = 4;
    float* m_dInputBuffers[NUM_BUFFERS] = {nullptr};
    float* m_dOutputBuffers[NUM_BUFFERS] = {nullptr};
    float* m_hOutputBuffers[NUM_BUFFERS] = {nullptr};
    cudaEvent_t m_events[NUM_BUFFERS] = {nullptr};
    
    // 相机过来的大图如果还没有锁页，可以在这里预先分配 Pinned Memory 用于中转
    uint8_t* m_hImagePool = nullptr;
    uint8_t* m_dImageBuffer = nullptr; // 大图 Device 端缓冲
};

#endif // INFERTHREAD_H
