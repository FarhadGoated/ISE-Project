#include "InventoryDialog.h"
#include "CheckoutStorage.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLockFile>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <limits>

namespace shop {

InventoryDialog::InventoryDialog(ProductCatalog& catalog, QWidget* parent, bool embedded)
    : QDialog(parent), catalog_(catalog)
{
    if (embedded) setWindowFlags(Qt::Widget);
    setWindowTitle("Inventory");
    auto* layout = new QVBoxLayout(this);
    auto* searchRow = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setPlaceholderText("Name, acronym, product ID or barcode");
    search_->setClearButtonEnabled(true);
    auto* searchLabel = new QLabel("&Search inventory", this);
    searchLabel->setBuddy(search_);
    matchCount_ = new QLabel(this);
    matchCount_->setProperty("muted", true);
    searchRow->addWidget(searchLabel);
    searchRow->addWidget(search_, 1);
    searchRow->addWidget(matchCount_);
    layout->addLayout(searchRow);

    table_ = new QTableWidget(0,6,this);
    table_->setHorizontalHeaderLabels({"ID","Product","Unit","Physical","Reserved","Available"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_);
    auto* controls = new QHBoxLayout;
    mode_ = new QComboBox(this);
    mode_->addItems({"Set stock","Add stock","Remove stock"});
    amount_ = new QLineEdit(this);
    amount_->setPlaceholderText("Quantity in the selected unit");
    auto* apply = new QPushButton("Apply",this);
    auto* reload = new QPushButton("Refresh",this);
    controls->addWidget(mode_); controls->addWidget(amount_,1);
    controls->addWidget(apply); controls->addWidget(reload);
    layout->addLayout(controls);
    status_ = new QLabel(this);
    layout->addWidget(status_);
    connect(apply,&QPushButton::clicked,this,&InventoryDialog::onSave);
    connect(search_, &QLineEdit::textChanged, this, &InventoryDialog::filterProducts);
    connect(reload,&QPushButton::clicked,this,&InventoryDialog::fillTable);
    if (!embedded) {
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close,this);
        layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    }
}

void InventoryDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    fillTable();
}

void InventoryDialog::fillTable()
{
    const QString path = catalog_.filePath();
    const QDir dir(catalog_.baseDirectory());
    QLockFile lock(dir.filePath(".checkout.lock"));
    table_->setRowCount(0);
    matchCount_->clear();
    if (!lock.tryLock(0)) { status_->setText("Inventory is busy. Refresh to try again."); return; }
    QString error;
    store::CheckoutStorage storage(dir.absolutePath(),nullptr);
    if (!catalog_.load(path,&error) || !storage.load()) {
        status_->setText(error.isEmpty() ? storage.loadErrors().join('\n') : error); return;
    }
    const auto reserved = storage.reservedByProduct(QDateTime::currentDateTime());
    for (const auto& p : catalog_.products()) {
        const auto held = reserved.value(p.id);
        const QStringList cells{p.id,p.name,p.unit == Unit::Weight ? "kg" : "pieces",
            formatQuantityMilli(p.stockMilli,p.unit),formatQuantityMilli(held,p.unit),
            formatQuantityMilli(qMax<qint64>(0,p.stockMilli-held),p.unit)};
        const int row = table_->rowCount(); table_->insertRow(row);
        for (int col=0; col<cells.size(); ++col) table_->setItem(row,col,new QTableWidgetItem(cells.at(col)));
        table_->item(row, 0)->setData(Qt::UserRole, p.barcode);
        table_->item(row, 0)->setData(Qt::UserRole + 1, p.acronym);
    }
    filterProducts();
    status_->setText("Select a product to adjust its physical stock.");
}

void InventoryDialog::filterProducts()
{
    const QString query = search_->text().trimmed();
    int matches = 0;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const auto* id = table_->item(row, 0);
        const bool visible = query.isEmpty()
            || id->text().contains(query, Qt::CaseInsensitive)
            || table_->item(row, 1)->text().contains(query, Qt::CaseInsensitive)
            || id->data(Qt::UserRole).toString().contains(query, Qt::CaseInsensitive)
            || id->data(Qt::UserRole + 1).toString().contains(query, Qt::CaseInsensitive);
        table_->setRowHidden(row, !visible);
        if (visible) ++matches;
    }
    const int selected = table_->currentRow();
    if (selected >= 0 && table_->isRowHidden(selected)) {
        table_->clearSelection();
        table_->setCurrentCell(-1, -1);
        amount_->clear();
    }
    matchCount_->setText(matches == 0 ? QStringLiteral("No matches")
                        : QStringLiteral("%1 of %2 products").arg(matches).arg(table_->rowCount()));
}

void InventoryDialog::onSave()
{
    const int row = table_->currentRow();
    if (row < 0 || table_->isRowHidden(row)) {
        status_->setText("Select a product first."); return;
    }
    const QString id = table_->item(row,0)->text();
    QLockFile lock(QDir(catalog_.baseDirectory()).filePath(".checkout.lock"));
    if (!lock.tryLock(0)) { status_->setText("Inventory is busy. Try again."); return; }
    ProductCatalog latest;
    Product p;
    QString error;
    store::CheckoutStorage storage(catalog_.baseDirectory(),nullptr);
    if (!latest.load(catalog_.filePath(),&error) || !storage.load() || !latest.findById(id,p)) {
        status_->setText(!error.isEmpty() ? error : "Cannot reload product and reservations."); return;
    }
    qint64 quantity = 0;
    if (!parseQuantityMilli(amount_->text().trimmed(),p.unit,quantity,&error)) { status_->setText(error); return; }
    if (mode_->currentIndex() == 1) {
        if (quantity > std::numeric_limits<qint64>::max()-p.stockMilli) { status_->setText("Quantity is too large."); return; }
        quantity += p.stockMilli;
    } else if (mode_->currentIndex() == 2) {
        quantity = p.stockMilli - quantity;
    }
    if (quantity < storage.reservedStock(id,QDateTime::currentDateTime())) {
        status_->setText("Physical stock must cover all active reservations."); return;
    }
    if (!latest.setStock(id,quantity,&error) || !latest.save(latest.filePath(),&error)) {
        status_->setText(error); return;
    }
    catalog_ = latest;
    lock.unlock();
    amount_->clear();
    fillTable();
    status_->setText("Stock updated for " + p.name);
}

} // namespace shop
