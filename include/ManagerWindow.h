#pragma once

class QWidget;
class QString;

namespace shop {

bool authorizeManager(QWidget* parent, const QString& action);
bool requestPaidEdit(QWidget* parent, const QString& dir, const QString& orderId,
                     const QString& activeBillId);
void finishPaidEdit(const QString& dir, const QString& orderId);

void openManagerWindow(QWidget* parent, const QString& dataDir);

} // namespace shop
