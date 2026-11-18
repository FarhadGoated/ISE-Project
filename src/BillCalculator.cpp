#include "BillCalculator.h"

namespace shop {

bool BillCalculator::calculate(const Order& order, Bill& bill, QString* error)
{
    // TODO(Shaffay): Validate and calculate line amounts, then publish the bill.
    if (error) {
        *error = QStringLiteral("Bill calculation is not implemented yet.");
    }
    Q_UNUSED(order);
    Q_UNUSED(bill);
    return false;
}

} // namespace shop
