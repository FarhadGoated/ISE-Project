#pragma once
#include <QString>

namespace shop {

class InputValidator {
public:
    static bool isValidSixDigitNumber(const QString& value,
                                      QString* error = nullptr);
};

}
