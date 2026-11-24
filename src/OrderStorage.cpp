#include "OrderStorage.h"

namespace shop {

bool loadOrder(const QString&, const QString&, Order&, QString& error)
{
    // TODO(Shahzeen): Restore an order and its saved details by Order ID.
    error = QStringLiteral("Saved-order loading is pending.");
    return false;
}

bool refreshOrderStates(const QString&, const QDateTime&, QString& error)
{
    // TODO(Shahzeen): Persist hold expiry and paid-order archival.
    error = QStringLiteral("Order state updates are pending.");
    return false;
}

} // namespace shop
