// Manager inventory and reservation-aware stock adjustments.
#pragma once
#include "ProductCatalog.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QLabel;
class QTableWidget;

namespace shop {

class InventoryDialog : public QDialog {
    Q_OBJECT
public:
    // `catalog` is changed in place after a successful save.
    explicit InventoryDialog(ProductCatalog& catalog, QWidget* parent = nullptr, bool embedded = false);

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void onSave();

private:
    void fillTable();
    void filterProducts();

    ProductCatalog& catalog_;
    QTableWidget* table_ = nullptr;
    QComboBox* mode_ = nullptr;
    QLineEdit* search_ = nullptr;
    QLabel* matchCount_ = nullptr;
    QLineEdit* amount_ = nullptr;
    QLabel* status_ = nullptr;
};

}
