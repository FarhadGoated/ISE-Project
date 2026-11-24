#include "BillStorage.h"

namespace shop {

bool loadBill(const QString&, const QString&, Bill&, QString& error)
{
    // TODO(Shahzeen): Read a bill summary and its lines by Bill ID.
    error = QStringLiteral("Bill loading is pending.");
    return false;
}

bool loadActiveBill(const QString&, const QString&, Bill&, QString& error)
{
    // TODO(Shahzeen): Resolve the current Bill ID for the given Order ID.
    error = QStringLiteral("Active bill lookup is pending.");
    return false;
}

} // namespace shop
