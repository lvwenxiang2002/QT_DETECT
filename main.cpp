#include "MainWindow.h"
#include <QApplication>
#include <opencv2/opencv.hpp>
#include "Common.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // 主动提权：将当前进程设置为高优先级，防止被 Windows 后台节流 (Background Throttling)
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif

    // 启用高分屏 (High-DPI) 缩放支持，保证 4K 和 1080P 下界面比例一致
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication a(argc, argv);
    
    // 限制 OpenCV 内部多线程，防止与 QThreadPool 抢占过多 CPU 资源导致界面和推理卡顿
    cv::setNumThreads(1);
    
    // 注册自定义类型以便跨线程信号槽传递
    qRegisterMetaType<std::shared_ptr<ImageFrame>>("std::shared_ptr<ImageFrame>");

    MainWindow w;
    w.resize(1600, 900); // 默认窗口大小
    w.show();

    return a.exec();
}
