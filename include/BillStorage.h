#pragma once

#include "Bill.h"

#include <QString>

namespace shop {

bool loadBill(const QString& dataDir, const QString& billId, Bill& bill, QString& error);
bool loadActiveBill(const QString& dataDir, const QString& orderId, Bill& bill, QString& error);

} // namespace shop
