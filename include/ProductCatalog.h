// ProductCatalog.h
#pragma once
#include "Product.h"
#include <QHash>
#include <QByteArray>
#include <QString>
#include <QVector>
namespace shop {

class ProductCatalog {
public:
    // ---- Shared handoff (exactly as declared in the guide) ----------------
    bool load(const QString& productPath, QString* error = nullptr);
    bool findByBarcode(const QString& barcode, Product& product) const;

    // ---- Additive API (nothing above changes) -----------------------------
    // Lookup by product ID (Shahzeen matches each bill line by ID + barcode).
    bool findById(const QString& id, Product& product) const;

    // All products in Product.txt order (for iteration / serialization).
    QVector<Product> products() const;
    int size() const;

    // Folder that contains Product.txt (absolute). Empty before load().
    QString baseDirectory() const;

    // Absolute path of the barcode image, resolved from Product.txt's folder,
    // never from the process working directory.
    QString resolveBarcodePath(const Product& product) const;

    // Stock update lives beside the serialization so the other nine fields
    // survive every checkout. Works on this in-memory catalog (call it on a
    // copy, then serialize()/save() the copy).
    bool deductStock(const QString& productId, qint64 quantityMilli,
                     QString* error = nullptr);

    // Product.txt text for the whole catalog (UTF-8, one row per product,
    // "\n" after every row, same field order as load()).
    QByteArray serialize() const;

    // Convenience: serialize() through QSaveFile (atomic replace).
    bool save(const QString& productPath, QString* error = nullptr) const;

private:
    QHash<QString, Product> products_;   // key: barcode
    QVector<QString> order_;             // barcodes in file order
    QString baseDir_;
};

// ---- Shared number helpers (money = paisa, quantity = thousandths) --------
// Used by the catalog, and available to Order UI, billing and storage so
// every screen and file prints the same text.

// "200.00" for 20000. Always two decimals; handles a leading '-'.
QString formatPaisa(qint64 paisa);

// Count -> "10", Weight -> "5.000" / "0.500" (always three decimals).
QString formatQuantityMilli(qint64 quantityMilli, Unit unit);

// Parse non-negative PKR text with 0-2 decimals ("200", "200.5", "200.50").
bool parsePaisa(const QString& text, qint64& paisa, QString* error = nullptr);

// Parse a quantity. Count: digits only. Weight: up to three decimals.
// Result is non-negative; callers apply their own "must be > 0" rule.
bool parseQuantityMilli(const QString& text, Unit unit, qint64& quantityMilli,
                        QString* error = nullptr);

// Six ASCII digits, leading zeros allowed. (See note in ProductCatalog.cpp:
// swap the body for the Increment 1 validator if it has a different name.)
bool isValidBarcodeText(const QString& barcode);

}
