// InventoryDialog.h
// Manager dialog: shows every product with its available quantity and lets the
// manager correct the quantities (e.g. after new stock arrives). Only the
// "Available" column is editable; "Save" writes Product.txt.
#pragma once
#include "ProductCatalog.h"

#include <QDialog>

class QLabel;
class QTableWidget;

namespace shop {

class InventoryDialog : public QDialog {
    Q_OBJECT
public:
    // `catalog` is changed in place after a successful save.
    explicit InventoryDialog(ProductCatalog& catalog, QWidget* parent = nullptr);

private slots:
    void onSave();

private:
    void fillTable();

    ProductCatalog& catalog_;
    QTableWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
};

}
