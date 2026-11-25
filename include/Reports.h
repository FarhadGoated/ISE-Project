#pragma once

class QWidget;
class QString;

namespace shop {

QWidget* makeReportsPage(const QString& dataDir, QWidget* parent);
void openReports(QWidget* parent, const QString& dataDir);

} // namespace shop
