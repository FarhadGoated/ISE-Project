#pragma once
#include "Order.h"

#include <QDate>
#include <QString>
#include <QTime>
#include <QVector>

namespace shop {
    struct Amounts {
        qint64 subtotalPaisa = 0;
        qint64 taxPaisa = 0;
        qint64 discountPaisa = 0;
        qint64 payablePaisa = 0;
    };

    struct BillLine {
        OrderItem item;
        Amounts amounts;
    };

    struct Bill {
        QVector<BillLine> lines;
        Amounts totals;

        qint64 seasonalDiscountPaisa = 0;
        qint64 specialDiscountPaisa = 0;
        int seasonalBasisPoints = 0;
        int specialBasisPoints = 0;

        qint64 amountReceivedPaisa = 0;
        qint64 balanceReturnedPaisa = 0;
        QString paymentMode;
        QString paymentProvider;
        QString paymentStatus;

        QString billId;
        QString orderId;
        QString customerName;
        QDate billDate;
        QTime billTime;
    };
}