#include "BillStorage.h"

#include "CheckoutStorage.h"
#include "ProductCatalog.h"

#include <QDir>
#include <QLockFile>

#include <limits>
#include <utility>

namespace shop {
namespace {

bool lineAmount(qint64 rate, qint64 quantity, qint64& amount)
{
    if (quantity <= 0 || rate < 0)
        return false;
    if (rate > (std::numeric_limits<qint64>::max() - 500) / quantity)
        return false;
    amount = (rate * quantity + 500) / 1000;
    return true;
}

bool readBill(const QString& dataDir, const QString& id, bool byOrder, Bill& bill, QString& error)
{
    error.clear();
    const QDir dir(dataDir);
    const QString key = id.trimmed();
    if (dataDir.isEmpty() || !dir.exists() || key.isEmpty()) {
        error = QStringLiteral("Provide a data folder and a bill or order ID.");
        return false;
    }

    QLockFile lock(dir.filePath(QStringLiteral(".checkout.lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        error = QStringLiteral("Another checkout operation is in progress. Try again.");
        return false;
    }

    store::CheckoutStorage storage(dataDir, nullptr);
    if (!storage.load()) {
        error = storage.loadErrors().join(QLatin1Char('\n'));
        return false;
    }
    store::BillRecord saved;
    const bool found = byOrder ? storage.activeBillForOrder(key, &saved) : storage.bill(key, &saved);
    if (!found) {
        error = byOrder ? QStringLiteral("Order %1 has no active bill.").arg(key)
                        : QStringLiteral("Bill %1 was not found.").arg(key);
        return false;
    }
    if (saved.lines.isEmpty()) {
        error = QStringLiteral("Bill %1 has no saved item rows.").arg(saved.billId);
        return false;
    }

    Bill restored;
    restored.billId = saved.billId;
    restored.orderId = saved.orderId;
    restored.customerName = saved.customerName;
    restored.billDate = saved.billDate;
    restored.billTime = saved.billTime;
    restored.paymentStatus = saved.paymentStatus;
    restored.amountReceivedPaisa = saved.amountReceived;
    restored.balanceReturnedPaisa = saved.balanceReturned;
    restored.seasonalDiscountPaisa = saved.seasonalDiscount;
    restored.specialDiscountPaisa = saved.specialDiscount;
    restored.totals.subtotalPaisa = saved.totalBeforeTax;
    restored.totals.taxPaisa = saved.totalTax;
    restored.totals.discountPaisa = saved.totalProductDiscount;
    restored.totals.payablePaisa = saved.totalPayable;

    store::OrderRecord order;
    const bool currentBill = storage.order(saved.orderId, &order) && order.activeBillId == saved.billId;
    const bool canMatchLines = currentBill && order.lines.size() == saved.lines.size();
    ProductCatalog catalog;
    restored.paymentMode = saved.paymentMode;
    restored.paymentProvider = saved.paymentMerchant;
    if (currentBill && restored.paymentMode.isEmpty()) {
        restored.paymentMode = order.paymentMode;
        restored.paymentProvider = order.paymentMerchant;
    }
    if (canMatchLines)
        catalog.load(dir.filePath(QStringLiteral("Product.txt")), nullptr);

    for (qsizetype row = 0; row < saved.lines.size(); ++row) {
        const store::BillLine& source = saved.lines.at(row);
        BillLine line;
        Product& product = line.item.product;
        const store::OrderLine* snapshot = canMatchLines ? &order.lines.at(row) : nullptr;
        if (snapshot && snapshot->acronym == source.acronym
            && snapshot->unitPricePaisa == source.unitPricePaisa
            && snapshot->qtyMilli == source.qtyMilli && snapshot->weighted == source.weighted) {
            catalog.findById(snapshot->productId, product);
            product.id = snapshot->productId;
            product.barcode = snapshot->barcode;
            product.taxPaisa = snapshot->taxPerUnitPaisa;
            product.discountPaisa = snapshot->discountPerUnitPaisa;
        }

        product.acronym = source.acronym;
        if (product.name.isEmpty())
            product.name = source.acronym;
        product.pricePaisa = source.unitPricePaisa;
        product.unit = source.weighted ? Unit::Weight : Unit::Count;
        line.item.quantityMilli = source.qtyMilli;
        line.amounts.discountPaisa = source.productDiscountPaisa;
        if (!lineAmount(source.unitPricePaisa, source.qtyMilli, line.amounts.subtotalPaisa)
            || !lineAmount(product.taxPaisa, source.qtyMilli, line.amounts.taxPaisa)) {
            error = QStringLiteral("Bill %1 contains an invalid item amount.").arg(saved.billId);
            return false;
        }
        restored.lines.push_back(std::move(line));
    }

    bill = std::move(restored);
    return true;
}

} // namespace

bool loadBill(const QString& dataDir, const QString& billId, Bill& bill, QString& error)
{
    return readBill(dataDir, billId, false, bill, error);
}

bool loadActiveBill(const QString& dataDir, const QString& orderId, Bill& bill, QString& error)
{
    return readBill(dataDir, orderId, true, bill, error);
}

} // namespace shop
