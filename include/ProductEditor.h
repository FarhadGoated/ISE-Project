// ProductEditor.h
// Manager dialog to add a new product or edit an existing one.
// - Adding: the manager picks a category (or creates one), fills the fields,
//   and the barcode (category id + 4-digit number) and its PNG are generated
//   on Save.
// - Editing preserves the Product ID across category and barcode changes.
// Both Product.txt and PCategory.txt are saved before the dialog closes.
#pragma once
#include "CategoryCatalog.h"
#include "ProductCatalog.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;

namespace shop {

class ProductEditor : public QDialog {
    Q_OBJECT
public:
    // editProductId empty -> add a new product; otherwise edit that product.
    // Both catalogs are changed in place only after a successful save.
    ProductEditor(ProductCatalog& catalog, CategoryCatalog& categories,
                  QWidget* parent = nullptr, const QString& editProductId = QString());

private slots:
    void onNewCategory();
    void onSave();

private:
    void fillCategoryCombo(const QString& selectId);
    bool readFields(Product& product, QString& error) const;

    ProductCatalog& catalog_;
    CategoryCatalog& categories_;
    Product original_;        // only meaningful when editing
    bool editing_ = false;

    QComboBox* categoryBox_ = nullptr;
    QLineEdit* idEdit_ = nullptr;
    QLineEdit* acronymEdit_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QComboBox* unitBox_ = nullptr;
    QLineEdit* priceEdit_ = nullptr;
    QLineEdit* taxEdit_ = nullptr;
    QLineEdit* discountEdit_ = nullptr;
    QLineEdit* quantityEdit_ = nullptr;
    QLineEdit* barcodeEdit_ = nullptr;
};

}
