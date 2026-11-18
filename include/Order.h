#pragma once

#include "Product.h"
#include <QVector>

namespace shop {

    struct OrderItem {
        Product product;
        qint64 quantityMilli = 0;
    };

    class Order {
    public:
        bool add(const Product& product, qint64 quantityMilli,
            QString* error = nullptr);
        bool setQuantity(const QString& productId, qint64 quantityMilli,
            QString* error = nullptr);
        bool remove(const QString& productId);
        const QVector<OrderItem>& items() const;

    private:
        QVector<OrderItem> items_;
    };

}