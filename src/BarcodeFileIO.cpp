#include "BarcodeFileIO.h"

namespace shop {

bool BarcodeFileIO::savePng(const QImage&, const QString&, QString* error)
{
    if (error) {
        *error = QStringLiteral("Saving barcode images is not implemented yet.");
    }
    return false;
}

QImage BarcodeFileIO::loadImage(const QString&, QString* error)
{
    if (error) {
        *error = QStringLiteral("Loading barcode images is not implemented yet.");
    }
    return {};
}

}