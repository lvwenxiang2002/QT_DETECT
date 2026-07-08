#include <QApplication>
#include <QImage>
#include <QPixmap>
#include <iostream>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    int w = 10000;
    int h = 32268;
    QImage img(w, h, QImage::Format_Grayscale8);
    img.fill(128); // gray
    QPixmap pixmap = QPixmap::fromImage(img);
    std::cout << "Pixmap isNull: " << pixmap.isNull() << std::endl;
    return 0;
}
