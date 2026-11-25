// InventoryDialog.cpp
#include "InventoryDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace shop {

namespace {
enum Column { IdColumn, BarcodeColumn, AcronymColumn, NameColumn, UnitColumn, AvailableColumn, ColumnCount };
}

InventoryDialog::InventoryDialog(ProductCatalog& catalog, QWidget* parent)
    : QDialog(parent), catalog_(catalog)
{
    setWindowTitle(QStringLiteral("Inventory"));
    resize(720, 480);

    table_ = new QTableWidget;
    table_->setColumnCount(ColumnCount);
    table_->setHorizontalHeaderLabels({QStringLiteral("Product ID"), QStringLiteral("Barcode"),
                                       QStringLiteral("Acronym"), QStringLiteral("Name"),
                                       QStringLiteral("Unit"), QStringLiteral("Available")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);

    status_ = new QLabel(QStringLiteral("Double-click an Available quantity to change it."));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::accepted, this, &InventoryDialog::onSave);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(table_);
    layout->addWidget(status_);
    layout->addWidget(buttons);

    fillTable();
}

void InventoryDialog::fillTable()
{
    const QVector<Product> list = catalog_.products();
    table_->setRowCount(static_cast<int>(list.size()));

    for (int row = 0; row < list.size(); ++row) {
        const Product& p = list.at(row);
        const QStringList texts = {
            p.id, p.barcode, p.acronym, p.name,
            p.unit == Unit::Weight ? QStringLiteral("kg") : QStringLiteral("count"),
            formatQuantityMilli(p.stockMilli, p.unit)};

        for (int col = 0; col < ColumnCount; ++col) {
            auto* item = new QTableWidgetItem(texts.at(col));
            if (col != AvailableColumn) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            table_->setItem(row, col, item);
        }
    }
    table_->resizeColumnsToContents();
}

void InventoryDialog::onSave()
{
    // Apply every change to a copy; nothing is kept unless all rows are valid and the file is saved.
    ProductCatalog updated = catalog_;
    const QVector<Product> list = catalog_.products();
    bool anyChange = false;

    for (int row = 0; row < list.size(); ++row) {
        const Product& p = list.at(row);
        const QString text = table_->item(row, AvailableColumn)->text().trimmed();

        qint64 quantity = 0;
        QString error;
        if (!parseQuantityMilli(text, p.unit, quantity, &error) || !updated.setStock(p.id, quantity, &error)) {
            table_->setCurrentCell(row, AvailableColumn);
            QMessageBox::warning(this, windowTitle(),
                                 QStringLiteral("%1 (%2): %3").arg(p.name, p.id, error));
            return;
        }
        if (quantity != p.stockMilli) anyChange = true;
    }

    if (!anyChange) {
        status_->setText(QStringLiteral("No changes to save."));
        return;
    }

    QString error;
    if (!updated.save(updated.filePath(), &error)) {
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }
    catalog_ = updated;
    fillTable();                                        // show the saved text format (e.g. 5 -> 5.000 for weight)
    status_->setText(QStringLiteral("Inventory saved."));
}

}
