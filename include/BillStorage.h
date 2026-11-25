#pragma once

#include "Bill.h"

#include <QString>

namespace shop {

// Bill IDs also resolve superseded bills, preserving their recorded amounts and lines.
bool loadBill(const QString& dataDir, const QString& billId, Bill& bill, QString& error);

// An order ID resolves its current bill for reprint.
bool loadActiveBill(const QString& dataDir, const QString& orderId, Bill& bill, QString& error);

} // namespace shop
