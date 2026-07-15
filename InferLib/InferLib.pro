TEMPLATE = lib
CONFIG += dll release
TARGET = InferLib

# CUDA & TensorRT include paths
INCLUDEPATH += "D:/software/opencvCuda48/opencv-4.8.0/build/install/include" \
               "D:/software/TensorRT-8.5.1.7/include" \
               "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8/include" \
               "C:/Program Files/I-TEK OptoElectronics/IKapLibrary/Include"

# CUDA & TensorRT lib paths
LIBS += -L"D:/software/opencvCuda48/opencv-4.8.0/build/install/x64/vc16/lib" \
        -lopencv_world480 \
        -L"D:/software/TensorRT-8.5.1.7/lib" \
        -lnvinfer \
        -L"C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8/lib/x64" \
        -lcudart

DEFINES += INFERLIB_EXPORTS

HEADERS += InferWrapper.h \
           InferAPI.h \
           InferKernels.h

SOURCES += InferWrapper.cpp

# CUDA compile rules
cuda.commands = C:/Progra~1/NVIDIA~2/CUDA/v11.8/bin/nvcc.exe -c --use_fast_math -O3 -Xcompiler /wd4819 -Xcompiler /utf-8 -m64 -Xcompiler /MD -I"D:/software/opencvCuda48/opencv-4.8.0/build/install/include" -I"D:/software/TensorRT-8.5.1.7/include" -I"C:/Progra~1/NVIDIA~2/CUDA/v11.8/include" ${QMAKE_FILE_NAME} -o ${QMAKE_FILE_OUT}
cuda.dependency_type = TYPE_C
cuda.depend_command = C:/Progra~1/NVIDIA~2/CUDA/v11.8/bin/nvcc.exe -M ${QMAKE_FILE_NAME}
cuda.input = CUDA_SOURCES
cuda.output = ${QMAKE_FILE_BASE}_cuda.obj
cuda.variable_out = OBJECTS
QMAKE_EXTRA_COMPILERS += cuda

CUDA_SOURCES += InferKernels.cu
