// Product.h  (shared declaration - do not change without telling the team)
#pragma once
#include <QtGlobal>
#include <QString>
namespace shop {
enum class Unit { Count, Weight };
struct Product {
    QString id, barcode, acronym, name;
    Unit unit = Unit::Count;
    qint64 pricePaisa = 0;
    qint64 taxPaisa = 0;
    qint64 discountPaisa = 0;
    qint64 stockMilli = 0;
    QString barcodePath;
};
}
