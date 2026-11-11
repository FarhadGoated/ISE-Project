#include "BarcodeReader.h"

namespace shop {

bool BarcodeReader::decodeCode39Digits(const std::vector<Run>&,
                                       QString& digits,
                                       QString* error)
{
    digits.clear();
    if (error) {
        *error = QStringLiteral("Barcode decoding is not implemented yet.");
    }
    return false;
}

}