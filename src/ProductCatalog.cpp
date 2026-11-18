#include "ProductCatalog.h"

namespace shop {

bool ProductCatalog::load(const QString& productPath, QString* error)
{
    // TODO(Anaya): Parse Product.txt into a temporary map and publish it on success.
    if (error) {
        *error = QStringLiteral("Product catalog loading is not implemented yet.");
    }
    Q_UNUSED(productPath);
    return false;
}

bool ProductCatalog::findByBarcode(const QString& barcode, Product& product) const
{
    const auto match = products_.constFind(barcode);
    if (match == products_.cend()) {
        return false;
    }
    product = match.value();
    return true;
}

} // namespace shop
