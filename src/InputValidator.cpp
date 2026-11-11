#include "InputValidator.h"

namespace shop {

bool InputValidator::isValidSixDigitNumber(const QString&, QString* error)
{
    if (error) {
        *error = QStringLiteral("Input validation is not implemented yet.");
    }
    return false;
}

}