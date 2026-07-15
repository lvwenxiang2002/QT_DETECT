#ifndef INFER_WRAPPER_H
#define INFER_WRAPPER_H

#include "InferAPI.h"
#include <NvInfer.h>
#include <cuda_runtime.h>
#include <string>
#include <vector>
#include <mutex>
#include <future>
#include <memory>

class TRTLogger : public nvinfer1::ILogger {
    void log(Severity severity, const char* msg) noexcept override;
};

class InferWrapper : public IInferenceEngine {
public:
    InferWrapper();
    ~InferWrapper() override;

    bool init(const char* modelBacklightPath, const char* modelDarkfieldPath, int batchSize) override;
    
    bool process(unsigned char* imageData, int width, int height, 
                 const InferConfig& configBacklight,
                 const InferConfig& configDarkfield,
                 InferResult* outResults, int maxResults, int& outCount) override;
                 
    void release() override;

private:
    void initMemoryPool();
    void releaseMemoryPool();
    
    void postProcess(float* h_out, nvinfer1::Dims out_dims, int crop_x, int crop_y, 
                     const InferConfig& config, int lightField, std::vector<InferResult>& localResults);

    TRTLogger m_logger;
    nvinfer1::IRuntime* m_runtime = nullptr;
    
    nvinfer1::ICudaEngine* m_engineBacklight = nullptr;
    nvinfer1::IExecutionContext* m_contextBacklight = nullptr;

    nvinfer1::ICudaEngine* m_engineDarkfield = nullptr;
    nvinfer1::IExecutionContext* m_contextDarkfield = nullptr;

    cudaStream_t m_stream;
    
    static const int NUM_BUFFERS = 4;
    float* m_dInputBuffers[NUM_BUFFERS] = {nullptr};
    float* m_dOutputBuffers[NUM_BUFFERS] = {nullptr};
    float* m_hOutputBuffers[NUM_BUFFERS] = {nullptr};
    cudaEvent_t m_events[NUM_BUFFERS] = {nullptr};
    
    uint8_t* m_dImageBuffer = nullptr; 
};

#endif // INFER_WRAPPER_H
