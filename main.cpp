#include "MainWindow.h"
#include "PosTheme.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    shop::applyPosTheme(app);
    shop::MainWindow window;
    window.show();
    return app.exec();
}
