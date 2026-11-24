#pragma once

#include <QRect>
#include <QSize>
#include <QString>

class QWidget;

namespace shop {

QString scanProductBarcode(QWidget* parent);
QRect mapScanGuideToFrame(const QRect& guide, const QRect& displayedImage,
                         const QSize& frameSize);

} // namespace shop
