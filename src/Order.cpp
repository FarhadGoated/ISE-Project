#include "Order.h"

#include <limits>
#include <utility>

namespace shop {

    namespace {

        constexpr int kHoldMinutes = 30;
        constexpr qint64 kEditWindowHours = 168;

        bool fail(QString* error, const QString& message) {
            if (error) {
                *error = message;
            }
            return false;
        }

        qint64 minUnitMilli(const Product& product) {
            return product.unit == Unit::Count ? 1000 : 1;
        }

        bool validateQuantity(const Product& product, qint64 quantityMilli,
            QString* error) {
            if (quantityMilli <= 0) {
                return fail(error, QStringLiteral("Quantity must be greater than zero."));
            }
            if (product.unit == Unit::Count && quantityMilli % 1000 != 0) {
                return fail(error, QStringLiteral("Enter a whole number of units."));
            }
            return true;
        }

        AddOutcome decide(qint64 available, const Product& product, qint64 total,
            AtLimitChoice choice, qint64& finalMilli, QString* error) {
            if (total > available) {
                fail(error, QStringLiteral("Not enough stock available."));
                return AddOutcome::Rejected;
            }
            if (total == available) {
                if (choice == AtLimitChoice::Ask) {
                    return AddOutcome::NeedsDecision;
                }
                if (choice == AtLimitChoice::KeepOne) {
                    const qint64 left = total - minUnitMilli(product);
                    if (left <= 0) {
                        fail(error, QStringLiteral("Cannot keep one: only one unit is available."));
                        return AddOutcome::Rejected;
                    }
                    finalMilli = left;
                    return AddOutcome::Added;
                }
            }
            finalMilli = total;
            return AddOutcome::Added;
        }

    }  // namespace

    Order::Order(AvailabilityFn availability)
        : availability_(std::move(availability)) {
    }

    AddOutcome Order::add(const Product& product, qint64 quantityMilli,
        QString* error, AtLimitChoice choice) {
        if (error) {
            error->clear();
        }
        if (!validateQuantity(product, quantityMilli, error)) {
            return AddOutcome::Rejected;
        }

        int index = -1;
        for (int i = 0; i < items_.size(); ++i) {
            if (items_[i].product.id == product.id) {
                index = i;
                break;
            }
        }

        qint64 total = quantityMilli;
        if (index >= 0) {
            const qint64 existing = items_[index].quantityMilli;
            if (existing > std::numeric_limits<qint64>::max() - quantityMilli) {
                fail(error, QStringLiteral("Quantity is too large."));
                return AddOutcome::Rejected;
            }
            total = existing + quantityMilli;
        }

        const qint64 available =
            availability_ ? availability_(product) : product.stockMilli;
        qint64 finalMilli = 0;
        const AddOutcome outcome =
            decide(available, product, total, choice, finalMilli, error);
        if (outcome != AddOutcome::Added) {
            return outcome;
        }

        if (index >= 0) {
            items_[index].quantityMilli = finalMilli;
        }
        else {
            OrderItem item;
            item.product = product;
            item.quantityMilli = finalMilli;
            items_.push_back(item);
        }
        return AddOutcome::Added;
    }

    AddOutcome Order::setQuantity(const QString& productId, qint64 quantityMilli,
        QString* error, AtLimitChoice choice) {
        if (error) {
            error->clear();
        }
        for (OrderItem& item : items_) {
            if (item.product.id != productId) {
                continue;
            }
            if (!validateQuantity(item.product, quantityMilli, error)) {
                return AddOutcome::Rejected;
            }
            const qint64 available =
                availability_ ? availability_(item.product) : item.product.stockMilli;
            qint64 finalMilli = 0;
            const AddOutcome outcome = decide(available, item.product, quantityMilli,
                choice, finalMilli, error);
            if (outcome == AddOutcome::Added) {
                item.quantityMilli = finalMilli;
            }
            return outcome;
        }
        fail(error, QStringLiteral("That item is not in the order."));
        return AddOutcome::Rejected;
    }

    bool Order::remove(const QString& productId) {
        for (int i = 0; i < items_.size(); ++i) {
            if (items_[i].product.id == productId) {
                items_.removeAt(i);
                return true;
            }
        }
        return false;
    }

    const QVector<OrderItem>& Order::items() const {
        return items_;
    }

    bool Order::hold(const QDateTime& now, QString* error) {
        if (error) {
            error->clear();
        }
        if (status_ != OrderStatus::InProgress) {
            return fail(error, QStringLiteral("Only an in-progress order can be held."));
        }
        if (items_.isEmpty()) {
            return fail(error, QStringLiteral("Cannot hold an empty order."));
        }
        holdExpiry_ = now.addSecs(kHoldMinutes * 60);
        status_ = OrderStatus::OnHold;
        return true;
    }

    bool Order::resume(const QDateTime& now, QString* error) {
        if (error) {
            error->clear();
        }
        if (status_ != OrderStatus::OnHold) {
            return fail(error, QStringLiteral("This order is not on hold."));
        }
        if (hasExpired(now)) {
            return fail(error, QStringLiteral("This held order has expired."));
        }
        status_ = OrderStatus::InProgress;
        return true;
    }

    bool Order::hasExpired(const QDateTime& now) const {
        const bool open = status_ == OrderStatus::OnHold ||
            status_ == OrderStatus::InProgress;
        return open && holdExpiry_.isValid() && now >= holdExpiry_;
    }

    bool Order::reprice(const ProductLookupFn& lookup,
        std::vector<PriceChange>& changes, QString* error) {
        changes.clear();
        if (error) {
            error->clear();
        }

        QVector<Product> current;
        for (const OrderItem& item : items_) {
            Product latest;
            if (!lookup || !lookup(item.product.id, latest)) {
                return fail(error, QStringLiteral("A product in this order no longer exists: ")
                    + item.product.name);
            }
            current.push_back(latest);
        }

        for (int i = 0; i < items_.size(); ++i) {
            Product& snap = items_[i].product;
            const Product& latest = current[i];
            if (snap.pricePaisa != latest.pricePaisa ||
                snap.discountPaisa != latest.discountPaisa) {
                changes.push_back({ snap.id, snap.pricePaisa, latest.pricePaisa,
                                   snap.discountPaisa, latest.discountPaisa });
            }
            snap = latest;
        }
        return true;
    }

    void Order::markPaid(const QDateTime& paidAt) {
        status_ = OrderStatus::Paid;
        paidAt_ = paidAt;
        holdExpiry_ = QDateTime();
    }

    bool Order::canEditPaid(const QDateTime& now) const {
        return status_ == OrderStatus::Paid && paidAt_.isValid() &&
            paidAt_.secsTo(now) < kEditWindowHours * 3600;
    }

    bool Order::archiveIfDue(const QDateTime& now) {
        if (status_ == OrderStatus::Paid && paidAt_.isValid() &&
            paidAt_.secsTo(now) >= kEditWindowHours * 3600) {
            status_ = OrderStatus::Archived;
            return true;
        }
        return false;
    }

    QHash<QString, qint64> Order::stockDeltas(const Order& before, const Order& after) {
        QHash<QString, qint64> delta;
        for (const OrderItem& item : before.items_) {
            delta[item.product.id] += item.quantityMilli;
        }
        for (const OrderItem& item : after.items_) {
            delta[item.product.id] -= item.quantityMilli;
        }
        for (auto it = delta.begin(); it != delta.end();) {
            it = (it.value() == 0) ? delta.erase(it) : ++it;
        }
        return delta;
    }

    void Order::setOrderId(const QString& id) { orderId_ = id; }
    void Order::restoreHold(const QDateTime& expiry) {
        holdExpiry_ = expiry;
        status_ = OrderStatus::OnHold;
    }
    void Order::restorePaid(const QDateTime& paidAt, OrderStatus status) {
        paidAt_ = paidAt;
        status_ = status;
    }
    const QString& Order::orderId() const { return orderId_; }
    OrderStatus Order::status() const { return status_; }
    const QDateTime& Order::holdExpiry() const { return holdExpiry_; }
    const QDateTime& Order::paidAt() const { return paidAt_; }

}  // namespace shop