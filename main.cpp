#include "MainWindow.h"
#include <QApplication>
#include "Common.h"

int main(int argc, char *argv[])
{
    // 启用高分屏 (High-DPI) 缩放支持，保证 4K 和 1080P 下界面比例一致
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication a(argc, argv);
    
    // 注册自定义类型以便跨线程信号槽传递
    qRegisterMetaType<std::shared_ptr<ImageFrame>>("std::shared_ptr<ImageFrame>");

    MainWindow w;
    w.resize(1600, 900); // 默认窗口大小
    w.show();

    return a.exec();
}
