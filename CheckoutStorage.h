// CheckoutStorage.h
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
    // Validates the checkout, rechecks stock against the latest Product.txt,
    // assigns IDs, then writes Order.txt, OrderDetail.txt, Bill.txt and
    // Product.txt (stock). If any commit fails, earlier files are restored.
    // receipt is cleared on entry and filled only after every write succeeds.
    static bool save(const QString& dataDir, const Bill& bill,
                     const CheckoutDetails& details, const QDateTime& now,
                     CheckoutReceipt& receipt, QString* error = nullptr);
};

}
