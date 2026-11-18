#include "Order.h"

#include <limits>

namespace shop {

    namespace {

        bool fail(QString* error, const QString& message) {
            if (error) {
                *error = message;
            }
            return false;
        }

        bool withinStock(const Product& product, qint64 totalMilli) {
            return totalMilli < product.stockMilli;
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

    }  // namespace

    bool Order::add(const Product& product, qint64 quantityMilli, QString* error) {
        if (error) {
            error->clear();
        }

        if (!validateQuantity(product, quantityMilli, error)) {
            return false;
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
                return fail(error, QStringLiteral("Quantity is too large."));
            }
            total = existing + quantityMilli;
        }

        if (!withinStock(product, total)) {
            return fail(error, QStringLiteral("Not enough stock available."));
        }

        if (index >= 0) {
            items_[index].quantityMilli = total;
        }
        else {
            OrderItem item;
            item.product = product;
            item.quantityMilli = quantityMilli;
            items_.push_back(item);
        }
        return true;
    }

    bool Order::setQuantity(const QString& productId, qint64 quantityMilli,
        QString* error) {
        if (error) {
            error->clear();
        }

        for (OrderItem& item : items_) {
            if (item.product.id == productId) {
                if (!validateQuantity(item.product, quantityMilli, error)) {
                    return false;
                }
                if (!withinStock(item.product, quantityMilli)) {
                    return fail(error, QStringLiteral("Not enough stock available."));
                }
                item.quantityMilli = quantityMilli;
                return true;
            }
        }
        return fail(error, QStringLiteral("That item is not in the order."));
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

} 