#ifndef INFER_API_H
#define INFER_API_H

#ifdef INFERLIB_EXPORTS
#define INFER_API __declspec(dllexport)
#else
#define INFER_API __declspec(dllimport)
#endif

struct InferResult {
    float x;
    float y;
    float width;
    float height;
    float confidence;
    int classId;
    int lightField; // 0 for Backlight, 1 for Darkfield
};

struct InferConfig {
    float globalInferConfidence; 
    float nmsThreshold;
    int cropSize;
    int overlap;
};

class INFER_API IInferenceEngine {
public:
    virtual ~IInferenceEngine() {}
    
    // Initialize the TensorRT engines
    // Returns true if successful
    virtual bool init(const char* modelBacklightPath, const char* modelDarkfieldPath, int batchSize) = 0;
    
    // Process a full interleaved raw image (Height can be up to 100,000 lines)
    // The DLL will internally split the light fields, tile, run CUDA preprocessing, TensorRT inference, and NMS.
    virtual bool process(unsigned char* imageData, int width, int height, 
                         const InferConfig& configBacklight,
                         const InferConfig& configDarkfield,
                         InferResult* outResults, int maxResults, int& outCount) = 0;
                         
    // Release resources and CUDA context
    virtual void release() = 0;
};

// Factory functions
extern "C" {
    INFER_API IInferenceEngine* createInferenceEngine();
    INFER_API void destroyInferenceEngine(IInferenceEngine* engine);
}

#endif // INFER_API_H
