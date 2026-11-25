#pragma once

#include "Product.h"
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>
#include <functional>
#include <vector>

namespace shop {

    struct OrderItem {
        Product product;
        qint64 quantityMilli = 0;
    };

    enum class OrderStatus { InProgress, OnHold, Paid, Archived };
    enum class AddOutcome { Added, NeedsDecision, Rejected };
    enum class AtLimitChoice { Ask, SellAll, KeepOne };

    struct PriceChange {
        QString productId;
        qint64 oldPricePaisa = 0;
        qint64 newPricePaisa = 0;
        qint64 oldDiscountPaisa = 0;
        qint64 newDiscountPaisa = 0;
    };

    class Order {
    public:
        using AvailabilityFn = std::function<qint64(const Product&)>;
        using ProductLookupFn = std::function<bool(const QString&, Product&)>;

        explicit Order(AvailabilityFn availability = nullptr);

        AddOutcome add(const Product& product, qint64 quantityMilli,
            QString* error = nullptr,
            AtLimitChoice choice = AtLimitChoice::Ask);

        AddOutcome setQuantity(const QString& productId, qint64 quantityMilli,
            QString* error = nullptr,
            AtLimitChoice choice = AtLimitChoice::Ask);

        bool remove(const QString& productId);
        const QVector<OrderItem>& items() const;

        bool hold(const QDateTime& now, QString* error = nullptr);
        bool resume(const QDateTime& now, QString* error = nullptr);
        bool hasExpired(const QDateTime& now) const;

        bool reprice(const ProductLookupFn& lookup,
            std::vector<PriceChange>& changes,
            QString* error = nullptr);

        void markPaid(const QDateTime& paidAt);
        bool canEditPaid(const QDateTime& now) const;
        bool archiveIfDue(const QDateTime& now);

        static QHash<QString, qint64> stockDeltas(const Order& before,
            const Order& after);

        void setOrderId(const QString& id);
        void restoreHold(const QDateTime& expiry);
        void restorePaid(const QDateTime& paidAt, OrderStatus status);
        const QString& orderId() const;
        OrderStatus status() const;
        const QDateTime& holdExpiry() const;
        const QDateTime& paidAt() const;

    private:
        AvailabilityFn availability_;
        QVector<OrderItem> items_;
        QString orderId_;
        OrderStatus status_ = OrderStatus::InProgress;
        QDateTime holdExpiry_;
        QDateTime paidAt_;
    };

    inline bool operator!(AddOutcome outcome) {
        return outcome != AddOutcome::Added;
    }

}