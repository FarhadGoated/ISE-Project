#pragma once

#include <QRect>
#include <QSize>
#include <QString>
#include <functional>

class QWidget;

namespace shop {

QString scanProductBarcode(QWidget* parent);
void scanOrderBarcodes(QWidget* parent, const std::function<bool(QWidget*, const QString&)>& addBarcode);
QRect mapScanGuideToFrame(const QRect& guide, const QRect& displayedImage,
                         const QSize& frameSize);

} // namespace shop
