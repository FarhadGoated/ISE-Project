#include "InputValidator.h"

namespace shop {

    bool InputValidator::isValidSixDigitNumber(const QString& value, QString* error)
    {
        if (error) {
            error->clear();
        }

        if (value.size() != 6) {
            if (error) {
                *error = QStringLiteral("Enter exactly six digits.");
            }
            return false;
        }

        for (QChar ch : value) {
            const auto codePoint = ch.unicode();
            if (codePoint < '0' || codePoint > '9') {
                if (error) {
                    *error = QStringLiteral("Use digits 0 to 9 only.");
                }
                return false;
            }
        }

        if (value.at(0) == QLatin1Char('0')) {
            if (error) {
                *error = QStringLiteral("The first digit must be 1 to 9.");
            }
            return false;
        }

        return true;
    }

}