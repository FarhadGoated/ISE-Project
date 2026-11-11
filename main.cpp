#include "ISEBarcodeProject.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    ISEBarcodeProject window;
    window.show();
    return app.exec();
}
