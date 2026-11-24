#include "CategoryCatalog.h"

namespace shop {

bool loadCategories(const QString&, QVector<Category>&, QString& error)
{
    // TODO(Anaya): Parse PCategory.txt and validate two-digit, unique Category IDs.
    error = QStringLiteral("Category loading is pending.");
    return false;
}

bool saveCategories(const QString&, const QVector<Category>&, QString& error)
{
    // TODO(Anaya): Save category records and enforce the rules for in-use categories.
    error = QStringLiteral("Category saving is pending.");
    return false;
}

} // namespace shop
