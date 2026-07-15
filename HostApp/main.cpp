#include "MainWindow.h"
#include <QApplication>
#include <QMessageBox>
#include <opencv2/opencv.hpp>
#include "Common.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "IKapC.h"
#include "IKapBoard.h"
#include "modules/ConfigManager.h"
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QThread>
#include <QDir>
#include <iostream>

void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);

    QString txt;
    QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    
    QString fileName = "Unknown";
    if (context.file) {
        fileName = QString(context.file);
        int lastSlash = fileName.lastIndexOf('/');
        int lastBackslash = fileName.lastIndexOf('\\');
        int slashIdx = qMax(lastSlash, lastBackslash);
        if (slashIdx >= 0) {
            fileName = fileName.mid(slashIdx + 1);
        }
    }
    
    QString contextStr = QString("[%1:%2]").arg(fileName).arg(context.line);
    QString threadId = QString::number((quintptr)QThread::currentThreadId(), 16); // Hex thread ID

    switch (type) {
        case QtDebugMsg:    txt = QString("[%1] [DEBUG] [T:%2] %3 - %4").arg(timeStr, threadId, contextStr, msg); break;
        case QtInfoMsg:     txt = QString("[%1] [INFO ] [T:%2] %3 - %4").arg(timeStr, threadId, contextStr, msg); break;
        case QtWarningMsg:  txt = QString("[%1] [WARN ] [T:%2] %3 - %4").arg(timeStr, threadId, contextStr, msg); break;
        case QtCriticalMsg: txt = QString("[%1] [CRIT ] [T:%2] %3 - %4").arg(timeStr, threadId, contextStr, msg); break;
        case QtFatalMsg:    txt = QString("[%1] [FATAL] [T:%2] %3 - %4").arg(timeStr, threadId, contextStr, msg); break;
    }

    std::cout << txt.toLocal8Bit().constData() << std::endl;

    QDir().mkpath("logs");
    QString dateStr = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QFile outFile(QString("logs/HQ_HostApp_%1.log").arg(dateStr));
    if (outFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
        QTextStream ts(&outFile);
        ts << txt << "\n";
    }
}

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // 主动提权：将当前进程设置为高优先级，防止被 Windows 后台节流 (Background Throttling)
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif

    // 注册自定义日志处理程序，将日志写入控制台和文件
    qInstallMessageHandler(customMessageHandler);

    // 初始化埃科 SDK
    ItkManInitialize();

    // 启用高分屏 (High-DPI) 缩放支持，保证 4K 和 1080P 下界面比例一致
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication a(argc, argv);
    
#ifdef Q_OS_WIN
    HANDLE hMutex = CreateMutexA(NULL, FALSE, "HQ_HostApp_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        QMessageBox::warning(nullptr, QString::fromUtf8("警告"), QString::fromUtf8("聚玻Vision - AI智能检测系统 已经在运行中，请勿重复打开！"));
        return 0;
    }
#endif

    // 限制 OpenCV 内部多线程，防止与 QThreadPool 抢占过多 CPU 资源导致界面和推理卡顿
    cv::setNumThreads(1);
    
    // 注册自定义类型以便跨线程信号槽传递
    qRegisterMetaType<std::shared_ptr<ImageFrame>>("std::shared_ptr<ImageFrame>");

    // 应用程序启动时全局加载系统配置
    ConfigManager::instance().load();

    MainWindow w;
    w.resize(1600, 900); // 默认窗口大小
    w.show();

    int ret = a.exec();

    // 释放埃科 SDK
    ItkManTerminate();

    return ret;
}
