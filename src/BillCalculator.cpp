#include "BillCalculator.h"

#include <QChar>
#include <QStringList>
#include <limits>

namespace shop {

    namespace {

        constexpr qint64 maxValue = std::numeric_limits<qint64>::max();
        constexpr int maxSeasonalBasisPoints = 10000;
        constexpr int maxSpecialBasisPoints = 800;

        bool fail(QString* error, const QString& message) {
            if (error) {
                *error = message;
            }
            return false;
        }

        bool lineAmount(qint64 perUnitPaisa, qint64 quantityMilli, qint64& out) {
            if (perUnitPaisa < 0 || quantityMilli <= 0) {
                return false;
            }
            if (perUnitPaisa != 0 && quantityMilli > maxValue / perUnitPaisa) {
                return false;
            }
            const qint64 product = perUnitPaisa * quantityMilli;
            if (product > maxValue - 500) {
                return false;
            }
            out = (product + 500) / 1000;
            return true;
        }

        bool rateAmount(qint64 baseAmount, int basisPoints, qint64& out) {
            if (baseAmount < 0 || basisPoints < 0 || basisPoints > maxSeasonalBasisPoints) {
                return false;
            }
            if (baseAmount != 0 && basisPoints > (maxValue - 5000) / baseAmount) {
                return false;
            }
            out = (baseAmount * basisPoints + 5000) / 10000;
            return true;
        }

        bool addChecked(qint64 a, qint64 b, qint64& out) {
            if (a < 0 || b < 0 || a > maxValue - b) {
                return false;
            }
            out = a + b;
            return true;
        }

        bool payableFrom(const Amounts& amounts, qint64& payable) {
            qint64 gross = 0;
            if (!addChecked(amounts.subtotalPaisa, amounts.taxPaisa, gross)) {
                return false;
            }
            if (amounts.discountPaisa > gross) {
                return false;
            }
            payable = gross - amounts.discountPaisa;
            return true;
        }

        bool onlyDigits(const QString& text) {
            for (QChar ch : text) {
                if (ch.unicode() < '0' || ch.unicode() > '9') {
                    return false;
                }
            }
            return true;
        }

    }  // namespace

    bool BillCalculator::calculate(const Order& order, Bill& bill, QString* error) {
        return calculate(order, DiscountRates(), bill, error);
    }

    bool BillCalculator::calculate(const Order& order, const DiscountRates& rates,
        Bill& bill, QString* error) {
        if (error) {
            error->clear();
        }

        if (rates.seasonalBasisPoints < 0
            || rates.seasonalBasisPoints > maxSeasonalBasisPoints) {
            return fail(error, QStringLiteral("The seasonal discount must be between 0% and 100%."));
        }
        if (rates.specialBasisPoints < 0
            || rates.specialBasisPoints > maxSpecialBasisPoints) {
            return fail(error, QStringLiteral("The special discount must be between 0% and 8%."));
        }

        const QVector<OrderItem>& items = order.items();
        if (items.isEmpty()) {
            return fail(error, QStringLiteral("The order is empty."));
        }

        Bill result;

        for (const OrderItem& item : items) {
            BillLine line;
            line.item = item;

            const Product& product = item.product;
            const QString name = product.name;

            if (!lineAmount(product.pricePaisa, item.quantityMilli,
                line.amounts.subtotalPaisa)
                || !lineAmount(product.taxPaisa, item.quantityMilli,
                    line.amounts.taxPaisa)
                || !lineAmount(product.discountPaisa, item.quantityMilli,
                    line.amounts.discountPaisa)) {
                return fail(error, QStringLiteral("Cannot calculate the bill for %1.").arg(name));
            }

            if (!payableFrom(line.amounts, line.amounts.payablePaisa)) {
                return fail(error, QStringLiteral("The discount for %1 is larger than its price plus tax.").arg(name));
            }

            if (!addChecked(result.totals.subtotalPaisa, line.amounts.subtotalPaisa,
                result.totals.subtotalPaisa)
                || !addChecked(result.totals.taxPaisa, line.amounts.taxPaisa,
                    result.totals.taxPaisa)
                || !addChecked(result.totals.discountPaisa, line.amounts.discountPaisa,
                    result.totals.discountPaisa)) {
                return fail(error, QStringLiteral("The bill total is too large to calculate."));
            }

            result.lines.append(line);
        }

        Amounts& totals = result.totals;

        if (totals.discountPaisa > totals.subtotalPaisa) {
            return fail(error, QStringLiteral("The product discounts are larger than the subtotal."));
        }

        const qint64 seasonalBase = totals.subtotalPaisa - totals.discountPaisa;
        qint64 seasonalDiscount = 0;
        if (!rateAmount(seasonalBase, rates.seasonalBasisPoints, seasonalDiscount)) {
            return fail(error, QStringLiteral("Cannot calculate the seasonal discount."));
        }

        const qint64 specialBase = seasonalBase - seasonalDiscount;
        qint64 specialDiscount = 0;
        if (!rateAmount(specialBase, rates.specialBasisPoints, specialDiscount)) {
            return fail(error, QStringLiteral("Cannot calculate the special discount."));
        }

        const qint64 discounted = specialBase - specialDiscount;
        if (!addChecked(discounted, totals.taxPaisa, totals.payablePaisa)) {
            return fail(error, QStringLiteral("The bill total is too large to calculate."));
        }

        result.seasonalDiscountPaisa = seasonalDiscount;
        result.specialDiscountPaisa = specialDiscount;
        result.seasonalBasisPoints = rates.seasonalBasisPoints;
        result.specialBasisPoints = rates.specialBasisPoints;

        bill = result;
        return true;
    }

    int BillCalculator::clampSpecialBasisPoints(int basisPoints) {
        if (basisPoints < 0) {
            return 0;
        }
        if (basisPoints > maxSpecialBasisPoints) {
            return maxSpecialBasisPoints;
        }
        return basisPoints;
    }

    bool BillCalculator::parsePercentBasisPoints(const QString& text, int& basisPoints) {
        QString value = text.trimmed();
        if (value.endsWith(QLatin1Char('%'))) {
            value.chop(1);
            value = value.trimmed();
        }

        bool negative = false;
        if (value.startsWith(QLatin1Char('-'))) {
            negative = true;
            value.remove(0, 1);
        }
        if (value.isEmpty()) {
            return false;
        }

        const QStringList parts = value.split(QLatin1Char('.'));
        if (parts.size() > 2) {
            return false;
        }

        const QString whole = parts[0];
        const QString fraction = parts.size() == 2 ? parts[1] : QString();
        if (whole.isEmpty() || whole.size() > 4 || fraction.size() > 2) {
            return false;
        }
        if (!onlyDigits(whole) || !onlyDigits(fraction)) {
            return false;
        }

        int bps = whole.toInt() * 100;
        if (!fraction.isEmpty()) {
            int fractionBps = fraction.toInt();
            if (fraction.size() == 1) {
                fractionBps *= 10;
            }
            bps += fractionBps;
        }

        basisPoints = negative ? -bps : bps;
        return true;
    }

    QString BillCalculator::formatPercentBasisPoints(int basisPoints) {
        const int absolute = basisPoints < 0 ? -basisPoints : basisPoints;
        QString text = QString::number(absolute / 100);

        const int fraction = absolute % 100;
        if (fraction != 0) {
            QString digits = QStringLiteral("%1").arg(fraction, 2, 10, QLatin1Char('0'));
            if (digits.endsWith(QLatin1Char('0'))) {
                digits.chop(1);
            }
            text += QStringLiteral(".") + digits;
        }

        return basisPoints < 0 ? QStringLiteral("-") + text : text;
    }

}  // namespace shop