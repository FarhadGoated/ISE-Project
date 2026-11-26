#pragma once

#include <QRect>
#include <QSize>
#include <QString>
#include <QList>
#include <functional>

class QWidget;

namespace shop {

QString scanProductBarcode(QWidget* parent);
void scanOrderBarcodes(QWidget* parent,
                       const std::function<bool(QWidget*, const QString&)>& addBarcode);
struct ScanCartLine {
    QString product;
    QString quantity;
};

void scanOrderProducts(QWidget* parent,
                       const std::function<QString(QWidget*, const QString&)>& addProduct,
                       const std::function<QList<ScanCartLine>()>& cartLines = {});
QRect mapScanGuideToFrame(const QRect& guide, const QRect& displayedImage,
                         const QSize& frameSize);

} // namespace shop