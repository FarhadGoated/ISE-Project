#pragma once
#include "Product.h"
#include <QHash>

namespace shop {
class ProductCatalog {
public:
    bool load(const QString& productPath, QString* error = nullptr);
    bool findByBarcode(const QString& barcode, Product& product) const;

private:
    QHash<QString, Product> products_;
};
}
