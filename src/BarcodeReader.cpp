#include "BarcodeReader.h"
#include "Code39Table.h"
#include "InputValidator.h"

#include <QChar>
#include <QLatin1Char>

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

        const int narrowPx = runs[9].widthPx;
        const long long widePx = 3LL * narrowPx;

        QString candidate;

        for (int symbolIndex = 0; symbolIndex < 8; ++symbolIndex) {
            const int base = symbolIndex * 10;
            QString pattern;

            for (int i = 0; i < 9; ++i) {
                const int widthPx = runs[base + i].widthPx;
                if (widthPx == narrowPx) {
                    pattern.append(QLatin1Char('n'));
                }
                else if (static_cast<long long>(widthPx) == widePx) {
                    pattern.append(QLatin1Char('w'));
                }
                else {
                    return fail(error, QStringLiteral("No readable barcode: bar widths do not match the expected ratio."));
                }
            }

            if (symbolIndex < 7) {
                const Run& gap = runs[base + 9];
                if (gap.isBlack || gap.widthPx != narrowPx) {
                    return fail(error, QStringLiteral("No readable barcode: invalid gap between symbols."));
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