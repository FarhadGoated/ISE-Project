#include "OrderStorage.h"

#include "CheckoutStorage.h"
#include "ProductCatalog.h"

#include <QDir>
#include <QLockFile>

#include <limits>
#include <utility>

namespace shop {

bool loadOrder(const QString& dataDir, const QString& orderId, Order& order, QString& error)
{
    error.clear();
    const QDir dir(dataDir);
    if (dataDir.isEmpty() || !dir.exists() || orderId.trimmed().isEmpty()) {
        error = QStringLiteral("Provide a data folder and order ID.");
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
    store::OrderRecord saved;
    if (!storage.order(orderId.trimmed(), &saved)) {
        error = QStringLiteral("Order %1 was not found.").arg(orderId);
        return false;
    }

    const bool held = saved.status == store::Status::OnHold;
    const bool paid = saved.status == store::Status::Paid;
    const bool archived = saved.status == store::Status::Archived;
    if (!held && !paid && !archived && saved.status != store::Status::InProgress) {
        error = QStringLiteral("Order %1 has status %2.").arg(saved.orderId, saved.status);
        return false;
    }
    if (saved.lines.isEmpty()) {
        error = QStringLiteral("Order %1 has no saved items.").arg(saved.orderId);
        return false;
    }

    ProductCatalog catalog;
    if (!catalog.load(dir.filePath(QStringLiteral("Product.txt")), &error))
        return false;

    const auto reserved = storage.reservedByProduct(QDateTime::currentDateTime(), saved.orderId);
    QHash<QString, qint64> available;
    for (const Product& product : catalog.products())
        available.insert(product.id, qMax<qint64>(0, product.stockMilli - reserved.value(product.id)));

    // Saved lines must remain viewable when the physical stock has changed.
    for (const store::OrderLine& line : saved.lines) {
        const qint64 extra = available.value(line.productId);
        if (paid || archived) {
            const qint64 limit = std::numeric_limits<qint64>::max();
            if (line.qtyMilli > 0)
                available[line.productId] = extra > limit - line.qtyMilli ? limit : extra + line.qtyMilli;
        } else {
            available[line.productId] = qMax(extra, line.qtyMilli);
        }
    }

    Order restored([available](const Product& product) { return available.value(product.id); });
    for (const store::OrderLine& line : saved.lines) {
        Product product;
        catalog.findById(line.productId, product);
        product.id = line.productId;
        product.barcode = line.barcode;
        product.acronym = line.acronym;
        if (product.name.isEmpty())
            product.name = line.acronym.isEmpty() ? line.productId : line.acronym;
        product.unit = line.weighted ? Unit::Weight : Unit::Count;
        product.pricePaisa = line.unitPricePaisa;
        product.taxPaisa = line.taxPerUnitPaisa;
        product.discountPaisa = line.discountPerUnitPaisa;
        if (restored.add(product, line.qtyMilli, &error, AtLimitChoice::SellAll) != AddOutcome::Added)
            return false;
    }

    restored.setOrderId(saved.orderId);
    if (held)
        restored.restoreHold(saved.holdExpiry);
    else if (paid || archived)
        restored.restorePaid(saved.paidAt, archived ? OrderStatus::Archived : OrderStatus::Paid);

    order = std::move(restored);
    return true;
}

bool refreshOrderStates(const QString& dataDir, const QDateTime& now, QString& error)
{
    error.clear();
    const QDir dir(dataDir);
    if (dataDir.isEmpty() || !dir.exists() || !now.isValid()) {
        error = QStringLiteral("An order-state refresh requires a valid data folder and time.");
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
    const store::SweepResult result = storage.sweep(now);
    error = result.result.error;
    return result.result.ok;
}

} // namespace shop
