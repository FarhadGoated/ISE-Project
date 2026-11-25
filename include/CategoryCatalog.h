// CategoryCatalog.h
// Product categories kept in PCategory.txt. One line per category:
//     <id> <name> <acronym> #
// The 2-digit id becomes the first two digits of every barcode in that category.
#pragma once
#include <QString>
#include <QVector>

namespace shop {

struct Category {
    QString id;        // exactly two digits, e.g. "12"
    QString name;      // e.g. "Dairy"
    QString acronym;   // 1 to 8 characters, e.g. "DRY"
};

class CategoryCatalog {
public:
    // A missing file is not an error: it means no category has been set up yet.
    bool load(const QString& categoryPath, QString* error = nullptr);

    // Writes the whole file (atomic replace) to the path given to load().
    bool save(QString* error = nullptr) const;

    QVector<Category> categories() const;
    bool findById(const QString& id, Category& category) const;

    // Adds a category after checking it (2-digit id, unique id, name, acronym).
    bool add(const Category& category, QString* error = nullptr);

    bool edit(const Category& category, QString* error);
    bool removeUnused(const QString& id, const class ProductCatalog& products, QString* error);

    // Smallest unused id ("10" ... "99"); empty if all are taken.
    QString nextFreeId() const;

private:
    QVector<Category> categories_;
    QString filePath_;
};

}
