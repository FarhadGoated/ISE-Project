#pragma once
#include "Bill.h"
#include <QDateTime>

namespace shop {
struct CheckoutDetails {
    QString customerName, customerContact, paymentMode, merchantName;
    QString status = QStringLiteral("paid");
    qint64 paymentPaisa = 0;
};

struct CheckoutReceipt {
    QString orderId, billId;
    bool recoveryRequired = false;
};

class CheckoutStorage {
public:
    static bool save(const QString& dataDir, const Bill& bill,
                     const CheckoutDetails& details, const QDateTime& now,
                     CheckoutReceipt& receipt, QString* error = nullptr);
};
}
