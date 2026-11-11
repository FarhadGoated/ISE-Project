#include "MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    shop::MainWindow window;
    window.show();
    return app.exec();
}