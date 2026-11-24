#pragma once

#include "Bill.h"

#include <QString>

namespace shop {

bool exportBillPdf(const Bill& bill, const QString& destination, QString& error);

} // namespace shop
