#include "InferWrapper.h"
#include "InferKernels.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>

using namespace nvinfer1;

// DLL Entry points
extern "C" INFER_API IInferenceEngine* createInferenceEngine() {
    return new InferWrapper();
}

extern "C" INFER_API void destroyInferenceEngine(IInferenceEngine* engine) {
    if (engine) {
        delete engine;
    }
}

void TRTLogger::log(Severity severity, const char* msg) noexcept {
    if (severity <= Severity::kWARNING) {
        std::cerr << "[TRT] " << msg << std::endl;
    }
}

InferWrapper::InferWrapper() {
    cudaStreamCreate(&m_stream);
    initMemoryPool();
}

InferWrapper::~InferWrapper() {
    release();
}

void InferWrapper::initMemoryPool() {
    size_t max_image_size = 16384ULL * 120000ULL * sizeof(uint8_t);
    cudaMalloc((void**)&m_dImageBuffer, max_image_size);

    size_t in_size = 1 * 3 * 1280 * 1280 * sizeof(float);
    size_t out_size = 1 * 32 * 33600 * sizeof(float);
    
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        cudaMalloc((void**)&m_dInputBuffers[i], in_size);
        cudaMalloc((void**)&m_dOutputBuffers[i], out_size);
        cudaHostAlloc((void**)&m_hOutputBuffers[i], out_size, cudaHostAllocDefault);
        cudaEventCreate(&m_events[i]);
    }
}

void InferWrapper::releaseMemoryPool() {
    if (m_dImageBuffer) { cudaFree(m_dImageBuffer); m_dImageBuffer = nullptr; }
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        if (m_dInputBuffers[i]) { cudaFree(m_dInputBuffers[i]); m_dInputBuffers[i] = nullptr; }
        if (m_dOutputBuffers[i]) { cudaFree(m_dOutputBuffers[i]); m_dOutputBuffers[i] = nullptr; }
        if (m_hOutputBuffers[i]) { cudaFreeHost(m_hOutputBuffers[i]); m_hOutputBuffers[i] = nullptr; }
        if (m_events[i]) { cudaEventDestroy(m_events[i]); m_events[i] = nullptr; }
    }
}

bool InferWrapper::init(const char* modelBacklightPath, const char* modelDarkfieldPath, int batchSize) {
    m_runtime = createInferRuntime(m_logger);
    if (!m_runtime) return false;

    auto loadEngine = [this](const char* path, ICudaEngine*& engine, IExecutionContext*& context) -> bool {
        std::ifstream file(path, std::ios::binary);
        if (!file.good()) {
            std::cerr << "[InferWrapper] Cannot open engine file: " << path << std::endl;
            return false;
        }
        file.seekg(0, file.end);
        size_t size = file.tellg();
        file.seekg(0, file.beg);
        std::vector<char> trtModelStream(size);
        file.read(trtModelStream.data(), size);
        file.close();

        engine = m_runtime->deserializeCudaEngine(trtModelStream.data(), size);
        if (!engine) return false;
        context = engine->createExecutionContext();
        return context != nullptr;
    };

    if (modelBacklightPath && modelBacklightPath[0] != '\0') {
        loadEngine(modelBacklightPath, m_engineBacklight, m_contextBacklight);
    }
    
    if (modelDarkfieldPath && modelDarkfieldPath[0] != '\0') {
        loadEngine(modelDarkfieldPath, m_engineDarkfield, m_contextDarkfield);
    }

    return true;
}

void InferWrapper::release() {
    if (m_contextBacklight) { m_contextBacklight->destroy(); m_contextBacklight = nullptr; }
    if (m_engineBacklight) { m_engineBacklight->destroy(); m_engineBacklight = nullptr; }
    if (m_contextDarkfield) { m_contextDarkfield->destroy(); m_contextDarkfield = nullptr; }
    if (m_engineDarkfield) { m_engineDarkfield->destroy(); m_engineDarkfield = nullptr; }
    if (m_runtime) { m_runtime->destroy(); m_runtime = nullptr; }
    
    releaseMemoryPool();
    if (m_stream) { cudaStreamDestroy(m_stream); m_stream = nullptr; }
}

bool InferWrapper::process(unsigned char* imageData, int width, int height, 
                           const InferConfig& configBacklight,
                           const InferConfig& configDarkfield,
                           InferResult* outResults, int maxResults, int& outCount) {
    outCount = 0;
    if (!m_dImageBuffer) return false;
    
    size_t imgSize = (size_t)width * height * sizeof(uint8_t);
    cudaMemcpyAsync(m_dImageBuffer, imageData, imgSize, cudaMemcpyHostToDevice, m_stream);
    
    int split_h = height / 2;
    std::future<void> buffer_futures[NUM_BUFFERS];
    bool buffer_active[NUM_BUFFERS] = {false};
    int crop_idx = 0;
    
    std::mutex resultsMutex;
    std::vector<InferResult> globalResults;

    for (int lf = 0; lf < 2; ++lf) {
        const InferConfig& currentConfig = (lf == 0) ? configBacklight : configDarkfield;
        IExecutionContext* current_context = (lf == 0) ? m_contextBacklight : m_contextDarkfield;
        ICudaEngine* current_engine = (lf == 0) ? m_engineBacklight : m_engineDarkfield;
        
        if (!current_context || !current_engine) continue;

        int crop_size = currentConfig.cropSize;
        if (crop_size <= 0) crop_size = 1280;
        int overlap = currentConfig.overlap;
        int step = crop_size - overlap;

        for (int y = 0; y < split_h; ) {
            int current_y = y;
            if (current_y + crop_size > split_h) current_y = std::max(0, split_h - crop_size);

            for (int x = 0; x < width; ) {
                int current_x = x;
                if (current_x + crop_size > width) current_x = std::max(0, width - crop_size);
                
                int engine_h = crop_size;
                int engine_w = crop_size;
                int channels = 1;

                int nbBindings = current_engine->getNbBindings();
                for (int i = 0; i < nbBindings; ++i) {
                    if (current_engine->bindingIsInput(i)) {
                        nvinfer1::Dims dims = current_engine->getBindingDimensions(i);
                        for (int j = 0; j < dims.nbDims; ++j) {
                            if (dims.d[j] == -1) {
                                if (j == 0) dims.d[j] = 1;
                                else if (j == 1) dims.d[j] = 1;
                                else dims.d[j] = crop_size;
                            }
                        }
                        if (dims.nbDims >= 4) {
                            channels = dims.d[1];
                            engine_h = dims.d[2];
                            engine_w = dims.d[3];
                        }
                        current_context->setBindingDimensions(i, dims);
                    }
                }

                int b = crop_idx % NUM_BUFFERS;
                if (buffer_active[b]) {
                    buffer_futures[b].wait();
                }

                hpc_vision::launch_fused_preprocess_kernel(
                    m_dImageBuffer, width, height,
                    current_x, current_y, crop_size, overlap,
                    m_dInputBuffers[b], engine_w, engine_h,
                    lf, m_stream, channels
                );

                void* bindings[] = {m_dInputBuffers[b], m_dOutputBuffers[b]};
                current_context->enqueueV2(bindings, m_stream, nullptr);

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
                
                cudaMemcpyAsync(m_hOutputBuffers[b], m_dOutputBuffers[b], out_size * sizeof(float), cudaMemcpyDeviceToHost, m_stream);
                cudaEventRecord(m_events[b], m_stream);

                float* h_out = m_hOutputBuffers[b];
                cudaEvent_t event = m_events[b];
                
                buffer_futures[b] = std::async(std::launch::async, [this, h_out, out_dims, current_x, current_y, currentConfig, lf, event, &resultsMutex, &globalResults]() {
                    cudaEventSynchronize(event);
                    std::vector<InferResult> localResults;
                    postProcess(h_out, out_dims, current_x, current_y, currentConfig, lf, localResults);
                    
                    std::lock_guard<std::mutex> lock(resultsMutex);
                    globalResults.insert(globalResults.end(), localResults.begin(), localResults.end());
                });
                
                buffer_active[b] = true;
                crop_idx++;
                
                if (current_x == std::max(0, width - crop_size)) break;
                x += step;
            }
            if (current_y == std::max(0, split_h - crop_size)) break;
            y += step;
        }
    }
    
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        if (buffer_active[i]) {
            buffer_futures[i].wait();
        }
    }
    
    outCount = std::min((int)globalResults.size(), maxResults);
    for (int i = 0; i < outCount; i++) {
        outResults[i] = globalResults[i];
    }
    
    return true;
}

void InferWrapper::postProcess(float* h_out, nvinfer1::Dims out_dims, int crop_x, int crop_y, 
                               const InferConfig& config, int lightField, std::vector<InferResult>& localResults) {
    int num_anchors = 33600;
    int num_classes = 8;
    bool is_channels_first = true;

    if (out_dims.nbDims >= 3) {
        if (out_dims.d[1] < out_dims.d[2]) {
            is_channels_first = true;
            num_classes = out_dims.d[1] - 4;
            num_anchors = out_dims.d[2];
        } else {
            is_channels_first = false;
            num_anchors = out_dims.d[1];
            num_classes = out_dims.d[2] - 4;
        }
    }

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

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
        
        if (best_class >= 0) {
            if (max_conf >= config.globalInferConfidence) {
                float x1 = cx - w / 2.0f;
                float y1 = cy - h / 2.0f;
                boxes.push_back(cv::Rect(x1, y1, w, h));
                confidences.push_back(max_conf);
                class_ids.push_back(best_class);
            }
        }
    }

    std::vector<cv::Rect> offset_boxes;
    const int NMS_OFFSET = 4096;
    for (size_t i = 0; i < boxes.size(); i++) {
        cv::Rect r = boxes[i];
        r.x += class_ids[i] * NMS_OFFSET;
        r.y += class_ids[i] * NMS_OFFSET;
        offset_boxes.push_back(r);
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(offset_boxes, confidences, 0.0f, config.nmsThreshold, indices);

    for (int idx : indices) {
        cv::Rect box = boxes[idx];
        
        InferResult res;
        res.x = box.x + crop_x;
        res.y = box.y + crop_y;
        res.width = box.width;
        res.height = box.height;
        res.confidence = confidences[idx];
        res.classId = class_ids[idx];
        res.lightField = lightField;
        
        localResults.push_back(res);
    }
}
