#include "BarcodeGenerator.h"

#include "Code39Table.h"
#include "InputValidator.h"

#include <QPainter>

namespace shop {

// each symbol = 6*3 + 3*9 = 45 px
// total width = 30 + 8*45 + 7*3 + 30 = 441 px
QImage BarcodeGenerator::generateCode39(const QString& digits, QString* error)
{
    if (error != nullptr) {
        error->clear();
    }

    if (!InputValidator::isValidSixDigitNumber(digits, error)) {
        return QImage();
    }

    QString payload;
    payload.reserve(digits.size() + 2);
    payload.append(QLatin1Char('*'));
    payload.append(digits);
    payload.append(QLatin1Char('*'));

    constexpr int narrowPx = 3;
    constexpr int widePx = 9;
    constexpr int quietPx = 30;
    constexpr int heightPx = 140;
    constexpr int totalWidthPx = 441; // 30 + 8*45 + 7*3 + 30

    QImage img(totalWidthPx, heightPx, QImage::Format_RGB32);
    if (img.isNull()) {
        if (error != nullptr) {
            *error = QStringLiteral("Failed to allocate barcode image.");
        }
        return QImage();
    }
    img.fill(Qt::white);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, false);

    int x = quietPx;
    for (qsizetype symbolIndex = 0; symbolIndex < payload.size(); ++symbolIndex) {
        const QString pattern = Code39Table::patternFor(payload.at(symbolIndex));
        if (pattern.isEmpty()) {
            painter.end();
            if (error != nullptr) {
                *error = QStringLiteral("Unsupported Code 39 character.");
            }
            return QImage();
        }

        for (int elementIndex = 0; elementIndex < 9; ++elementIndex) {
            const int width = pattern.at(elementIndex) == QLatin1Char('w')
                                  ? widePx
                                  : narrowPx;
            if (elementIndex % 2 == 0) {
                painter.fillRect(x, 0, width, heightPx, Qt::black);
            }
            x += width;
        }

        if (symbolIndex + 1 < payload.size()) {
            x += narrowPx;
        }
    }

    painter.end();
    return img;
}

}
