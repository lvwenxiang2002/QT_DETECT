QT       += core gui concurrent
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = HQ_HostApp
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

CONFIG += c++11

msvc {
    QMAKE_CXXFLAGS += /utf-8
}

SOURCES += \
    main.cpp \
    MainWindow.cpp \
    widgets/ImageWidget.cpp \
    widgets/PieChartWidget.cpp \
    widgets/Dialogs.cpp \
    widgets/RoiSettingDialog.cpp \
    modules/CameraThread.cpp \
    modules/InferThread.cpp \
    modules/RecipeManager.cpp \
    modules/ConfigManager.cpp

HEADERS += \
    MainWindow.h \
    widgets/ImageWidget.h \
    widgets/PieChartWidget.h \
    widgets/Dialogs.h \
    widgets/RoiSettingDialog.h \
    modules/CameraThread.h \
    modules/InferThread.h \
    modules/RecipeManager.h \
    modules/ConfigManager.h \
    Common.h

# --- OpenCV CUDA ---
INCLUDEPATH += D:/SoftWare/opencvCuda48/opencv-4.8.0/build/install/include
LIBS += -LD:/SoftWare/opencvCuda48/opencv-4.8.0/build/install/x64/vc16/lib \
        -lopencv_world480

# --- TensorRT ---
INCLUDEPATH += D:/SoftWare/TensorRT-8.5.1.7/include
LIBS += -LD:/SoftWare/TensorRT-8.5.1.7/lib \
        -lnvinfer -lnvparsers -lnvinfer_plugin -lnvonnxparser

# --- CUDA ---
INCLUDEPATH += "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8/include"
LIBS += -L"C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8/lib/x64" \
        -lcudart -lcublas -lcudnn -lcurand

# --- I-TEK Camera SDK (IKap) ---
INCLUDEPATH += "C:/Program Files/I-TEK OptoElectronics/IKapLibrary/Include"
LIBS += -L"C:/Program Files/I-TEK OptoElectronics/IKapLibrary/Lib/x64" \
        -lIKapBoard -lIKapC

# --- NVCC CUDA Compiler Configuration ---
CUDA_SOURCES += modules/InferKernels.cu

CUDA_DIR = "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8"
NVCC_OPTIONS = --use_fast_math -O3 -Xcompiler /wd4819 -Xcompiler /utf-8 -m64 -Xcompiler /MD

# Add INCLUDEPATH correctly to NVCC
INCLUDEPATH_NVCC =
for(inc, INCLUDEPATH) {
    INCLUDEPATH_NVCC += -I\"$$inc\"
}

cuda.input = CUDA_SOURCES
cuda.output = ${QMAKE_FILE_BASE}_cuda.obj
cuda.commands = "\"$$CUDA_DIR/bin/nvcc.exe\"" -c $$NVCC_OPTIONS $$INCLUDEPATH_NVCC ${QMAKE_FILE_NAME} -o ${QMAKE_FILE_OUT}
cuda.dependency_type = TYPE_C
QMAKE_EXTRA_COMPILERS += cuda
