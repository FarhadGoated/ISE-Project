#include "CheckoutStorage.h"

namespace shop {

bool CheckoutStorage::save(const QString& dataDir, const Bill& bill,
                           const CheckoutDetails& details, const QDateTime& now,
                           CheckoutReceipt& receipt, QString* error)
{
    // TODO(Shahzeen): Validate records, assign IDs, and save the checkout.
    receipt = {};
    if (error) {
        *error = QStringLiteral("Checkout storage is not implemented yet.");
    }
    Q_UNUSED(dataDir);
    Q_UNUSED(bill);
    Q_UNUSED(details);
    Q_UNUSED(now);
    return false;
}

} // namespace shop
