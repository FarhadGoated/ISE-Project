#include "BarcodeGenerator.h"

namespace shop {

QImage BarcodeGenerator::generateCode39(const QString&, QString* error)
{
    if (error) {
        *error = QStringLiteral("Barcode generation is not implemented yet.");
    }
    return {};
}

}