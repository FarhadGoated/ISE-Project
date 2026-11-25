#pragma once

#include "Bill.h"

#include <QString>

class QWidget;

namespace shop {

	bool exportBillPdf(const Bill& bill, const QString& destination, QString& error);

	bool offerBillPdf(QWidget* parent, const Bill& bill);

	bool reprintBill(QWidget* parent, const QString& dataDir);

} // namespace shop