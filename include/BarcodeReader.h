#pragma once

#include "BarcodeTypes.h"

#include <QString>
#include <vector>

namespace shop {

class BarcodeReader {
public:
    static bool decodeCode39Digits(const std::vector<Run>& runs,
                                   QString& digits,
                                   QString* error = nullptr);
};

}