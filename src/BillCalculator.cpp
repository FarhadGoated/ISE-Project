#include "BillCalculator.h"

#include <limits>

namespace shop {

    namespace {

        constexpr qint64 maxValue = std::numeric_limits<qint64>::max();

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

    }  // namespace

    bool BillCalculator::calculate(const Order& order, Bill& bill, QString* error) {
        if (error) {
            error->clear();
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

        if (!payableFrom(result.totals, result.totals.payablePaisa)) {
            return fail(error, QStringLiteral("The total discount is larger than the total price plus tax."));
        }

        bill = result;
        return true;
    }

}  // namespace shop