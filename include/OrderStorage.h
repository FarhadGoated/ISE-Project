#pragma once

#include "Order.h"

#include <QDateTime>
#include <QString>

namespace shop {

bool loadOrder(const QString& dataDir, const QString& orderId, Order& order, QString& error);
bool refreshOrderStates(const QString& dataDir, const QDateTime& now, QString& error);

} // namespace shop
