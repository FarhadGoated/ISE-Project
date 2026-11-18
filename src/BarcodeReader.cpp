#include "BarcodeReader.h"
#include "Code39Table.h"
#include "InputValidator.h"

#include <QChar>
#include <QLatin1Char>
#include <algorithm>
#include <array>

namespace shop {

    namespace {

        bool fail(QString* error, const QString& message) {
            if (error) {
                *error = message;
            }
            return false;
        }

    }  // namespace

    bool BarcodeReader::decodeCode39Digits(const std::vector<Run>& runs,
        QString& digits,
        QString* error) {
        digits.clear();
        if (error) {
            error->clear();
        }

        if (runs.size() != 79) {
            return fail(error, QStringLiteral("No readable barcode: unexpected number of bars and spaces."));
        }
        for (std::size_t i = 0; i < runs.size(); ++i) {
            if (runs[i].widthPx <= 0) {
                return fail(error, QStringLiteral("No readable barcode: invalid bar width."));
            }
            const bool expectedBlack = (i % 2 == 0);
            if (runs[i].isBlack != expectedBlack) {
                return fail(error, QStringLiteral("No readable barcode: bars and spaces do not alternate."));
            }
        }

        QString candidate;

        for (int symbolIndex = 0; symbolIndex < 8; ++symbolIndex) {
            const int base = symbolIndex * 10;

            std::array<int, 9> widths{};
            for (int i = 0; i < 9; ++i) {
                widths[i] = runs[base + i].widthPx;
            }
            std::sort(widths.begin(), widths.end());

            const int maxNarrow = widths[5];
            const int minWide = widths[6];
            if (2LL * minWide < 3LL * maxNarrow
                || maxNarrow > 2LL * widths.front()
                || widths.back() > 2LL * minWide) {
                return fail(error, QStringLiteral("Unclear barcode run widths."));
            }

            QString pattern;
            for (int i = 0; i < 9; ++i) {
                const bool wide = runs[base + i].widthPx > maxNarrow;
                pattern.append(QLatin1Char(wide ? 'w' : 'n'));
            }

            if (symbolIndex < 7) {
                const Run& gap = runs[base + 9];
                const int typicalNarrow = widths[2];
                if (gap.isBlack || 2LL * gap.widthPx < typicalNarrow
                    || gap.widthPx > 2LL * typicalNarrow) {
                    return fail(error, QStringLiteral("Invalid gap between symbols."));
                }
            }

            const QChar ch = Code39Table::characterForPattern(pattern);
            if (ch.isNull()) {
                return fail(error, QStringLiteral("No readable barcode: unknown symbol pattern."));
            }

            const bool isFrameSymbol = (symbolIndex == 0 || symbolIndex == 7);
            if (isFrameSymbol) {
                if (ch != QLatin1Char('*')) {
                    return fail(error, QStringLiteral("No readable barcode: missing start or stop symbol."));
                }
            }
            else {
                if (ch == QLatin1Char('*')) {
                    return fail(error, QStringLiteral("No readable barcode: unexpected start or stop symbol inside the barcode."));
                }
                candidate.append(ch);
            }
        }

        if (!InputValidator::isValidSixDigitNumber(candidate, error)) {
            return false;
        }

        digits = candidate;
        return true;
    }

}  // namespace shop