#pragma once

#include "Order.h"

#include <QDateTime>
#include <QString>

namespace shop {

// Rebuild a cart from the original order-line snapshots and saved lifecycle state.
bool loadOrder(const QString& dataDir, const QString& orderId, Order& order, QString& error);

// Persist an expiry and archival sweep using the given time.
bool refreshOrderStates(const QString& dataDir, const QDateTime& now, QString& error);

} // namespace shop
