// ProductEditor.cpp
#include "ProductEditor.h"
#include "BarcodeGenerator.h"
#include "CheckoutStorage.h"
#include <QLockFile>
#include <QSaveFile>
#include <QFile>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

namespace shop {

namespace {

QString nextProductId(const ProductCatalog& products)
{
    Product existing;
    for (qint64 i = 1; ; ++i) {
        const QString id = QStringLiteral("P%1").arg(i, 3, 10, QLatin1Char('0'));
        if (!products.findById(id, existing)) return id;
    }
}

// Small dialog that asks for the three category fields. Returns true if the
// manager pressed OK; `out` is then filled (not yet validated).
bool askCategory(QWidget* parent, const QString& suggestedId, Category& out)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("New category"));

    auto* idEdit = new QLineEdit(suggestedId);
    idEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,2}")), idEdit));
    auto* nameEdit = new QLineEdit;
    auto* acronymEdit = new QLineEdit;
    acronymEdit->setMaxLength(8);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Category ID (2 digits)"), idEdit);
    form->addRow(QStringLiteral("Category name"), nameEdit);
    form->addRow(QStringLiteral("Acronym"), acronymEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) return false;
    out.id = idEdit->text().trimmed();
    out.name = nameEdit->text().trimmed();
    out.acronym = acronymEdit->text().trimmed();
    return true;
}

} // namespace

ProductEditor::ProductEditor(ProductCatalog& catalog, CategoryCatalog& categories,
                             QWidget* parent, const QString& editProductId)
    : QDialog(parent), catalog_(catalog), categories_(categories)
{
    editing_ = !editProductId.isEmpty();
    if (editing_ && !catalog_.findById(editProductId, original_)) editing_ = false;   // unknown ID: act as "add"
    setWindowTitle(editing_ ? QStringLiteral("Edit product") : QStringLiteral("Add product"));

    categoryBox_ = new QComboBox;
    auto* newCategoryButton = new QPushButton(QStringLiteral("New category..."));
    auto* categoryRow = new QHBoxLayout;
    categoryRow->addWidget(categoryBox_, 1);
    categoryRow->addWidget(newCategoryButton);

    idEdit_ = new QLineEdit;
    acronymEdit_ = new QLineEdit;
    acronymEdit_->setMaxLength(8);
    nameEdit_ = new QLineEdit;
    unitBox_ = new QComboBox;
    unitBox_->addItem(QStringLiteral("Count"), static_cast<int>(Unit::Count));
    unitBox_->addItem(QStringLiteral("Weight (kg)"), static_cast<int>(Unit::Weight));
    priceEdit_ = new QLineEdit;
    taxEdit_ = new QLineEdit;
    discountEdit_ = new QLineEdit;
    quantityEdit_ = new QLineEdit;
    barcodeEdit_ = new QLineEdit;
    barcodeEdit_->setMaxLength(6);
    barcodeEdit_->setPlaceholderText("Leave blank to generate from category");
    barcodeEdit_->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{0,6}"),barcodeEdit_));

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Category"), categoryRow);
    form->addRow(QStringLiteral("Product ID"), idEdit_);
    form->addRow(QStringLiteral("Acronym"), acronymEdit_);
    form->addRow(QStringLiteral("Name"), nameEdit_);
    form->addRow(QStringLiteral("Unit"), unitBox_);
    form->addRow(QStringLiteral("Unit price (PKR, no tax)"), priceEdit_);
    form->addRow(QStringLiteral("Unit tax (PKR)"), taxEdit_);
    form->addRow(QStringLiteral("Unit discount (PKR)"), discountEdit_);
    form->addRow(QStringLiteral("Available quantity"), quantityEdit_);
    form->addRow(QStringLiteral("Barcode"), barcodeEdit_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(newCategoryButton, &QPushButton::clicked, this, &ProductEditor::onNewCategory);
    connect(buttons, &QDialogButtonBox::accepted, this, &ProductEditor::onSave);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (editing_) {
        fillCategoryCombo(original_.categoryId);
        newCategoryButton->setEnabled(false);
        idEdit_->setText(original_.id);
        idEdit_->setReadOnly(true);               // other files refer to this ID
        acronymEdit_->setText(original_.acronym);
        nameEdit_->setText(original_.name);
        unitBox_->setCurrentIndex(original_.unit == Unit::Weight ? 1 : 0);
        priceEdit_->setText(formatPaisa(original_.pricePaisa));
        taxEdit_->setText(formatPaisa(original_.taxPaisa));
        discountEdit_->setText(formatPaisa(original_.discountPaisa));
        quantityEdit_->setText(formatQuantityMilli(original_.stockMilli, original_.unit));
        barcodeEdit_->setText(original_.barcode);
    } else {
        fillCategoryCombo(QString());
        discountEdit_->setText(QStringLiteral("0.00"));
        taxEdit_->setText(QStringLiteral("0.00"));
        quantityEdit_->setText(QStringLiteral("0"));
        idEdit_->setText(nextProductId(catalog_));
        idEdit_->setReadOnly(true);
    }
    connect(categoryBox_, &QComboBox::currentIndexChanged, this, [this] {
        if (editing_ && categoryBox_->currentData().toString() == original_.categoryId)
            barcodeEdit_->setText(original_.barcode);
        else barcodeEdit_->clear();
    });
}

void ProductEditor::fillCategoryCombo(const QString& selectId)
{
    categoryBox_->clear();
    for (const Category& c : categories_.categories())
        categoryBox_->addItem(QStringLiteral("%1 - %2 (%3)").arg(c.id, c.name, c.acronym), c.id);

    int index = categoryBox_->findData(selectId);
    if (index < 0 && !selectId.isEmpty()) {          // category missing from PCategory.txt: still show its ID
        categoryBox_->addItem(selectId, selectId);
        index = categoryBox_->count() - 1;
    }
    if (index >= 0) categoryBox_->setCurrentIndex(index);
}

void ProductEditor::onNewCategory()
{
    Category category;
    if (!askCategory(this, categories_.nextFreeId(), category)) return;

    // Work on a copy; the real catalog changes only if the file is saved.
    const QDir dir(catalog_.baseDirectory());
    QLockFile lock(dir.filePath(".checkout.lock"));
    if (!lock.tryLock(0)) { QMessageBox::warning(this,windowTitle(),"Catalog is busy."); return; }
    CategoryCatalog updated;
    QString error;
    if (!updated.load(dir.filePath("PCategory.txt"), &error)) { QMessageBox::warning(this,windowTitle(),error); return; }
    if (!updated.add(category, &error) || !updated.save(&error)) {
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }
    categories_ = updated;
    fillCategoryCombo(category.id);
}

bool ProductEditor::readFields(Product& p, QString& error) const
{
    p.id = idEdit_->text().trimmed();
    p.acronym = acronymEdit_->text().trimmed();
    p.name = nameEdit_->text().trimmed();
    p.unit = unitBox_->currentData().toInt() == static_cast<int>(Unit::Weight) ? Unit::Weight : Unit::Count;

    QString detail;
    if (!parsePaisa(priceEdit_->text().trimmed(), p.pricePaisa, &detail)) { error = QStringLiteral("Unit price: ") + detail; return false; }
    if (!parsePaisa(taxEdit_->text().trimmed(), p.taxPaisa, &detail)) { error = QStringLiteral("Unit tax: ") + detail; return false; }
    if (!parsePaisa(discountEdit_->text().trimmed(), p.discountPaisa, &detail)) { error = QStringLiteral("Unit discount: ") + detail; return false; }
    if (!parseQuantityMilli(quantityEdit_->text().trimmed(), p.unit, p.stockMilli, &detail)) { error = QStringLiteral("Available quantity: ") + detail; return false; }
    return true;
}

void ProductEditor::onSave()
{
    Product product;
    QString error;
    if (!readFields(product, error)) {
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }

    const QDir dir(catalog_.baseDirectory());
    QLockFile lock(dir.filePath(".checkout.lock"));
    if (!lock.tryLock(0)) { QMessageBox::warning(this,windowTitle(),"Catalog is busy."); return; }
    ProductCatalog updated;
    CategoryCatalog categories;
    store::CheckoutStorage storage(dir.absolutePath(), nullptr);
    if (!updated.load(catalog_.filePath(), &error) ||
        !categories.load(dir.filePath("PCategory.txt"), &error) || !storage.load()) {
        QMessageBox::warning(this,windowTitle(),error.isEmpty() ? storage.loadErrors().join('\n') : error); return;
    }
    if (editing_ && catalog_.serialize() != updated.serialize()) {
        QMessageBox::warning(this,windowTitle(),"The catalog changed. Reopen this product to edit its latest values."); return;
    }
    if (editing_ && product.unit != original_.unit) {
        for (const QString& id : storage.orderIds()) {
            store::OrderRecord record;
            storage.order(id,&record);
            for (const auto& line : record.lines) {
                if (line.productId == product.id) {
                    QMessageBox::warning(this,windowTitle(),"A saved order uses this unit. Create a new product for a different unit.");
                    return;
                }
            }
        }
    }
    if (!editing_) product.id = nextProductId(updated);
    product.categoryId = categoryBox_->currentData().toString();
    Category category;
    if (!categories.findById(product.categoryId, category)) {
        QMessageBox::warning(this,windowTitle(),"Select an existing category first."); return;
    }
    product.barcode = barcodeEdit_->text().trimmed();
    if (product.barcode.isEmpty()) {
        product.barcode = editing_ && original_.categoryId == product.categoryId
            ? original_.barcode : updated.nextBarcode(product.categoryId);
    }
    product.barcodePath = editing_ && product.barcode == original_.barcode
        ? original_.barcodePath : QStringLiteral("barcodes/%1.png").arg(product.barcode);
    if (product.stockMilli < storage.reservedStock(product.id,QDateTime::currentDateTime())) {
        QMessageBox::warning(this,windowTitle(),"Physical stock cannot be lower than reserved stock."); return;
    }
    if (!(editing_ ? updated.updateProduct(product,&error) : updated.addProduct(product,&error))) {
        QMessageBox::warning(this,windowTitle(),error); return;
    }
    const QString path = updated.resolveBarcodePath(product);
    const bool createImage = !editing_ || product.barcode != original_.barcode || !QFileInfo::exists(path);
    const bool fileExisted = QFileInfo::exists(path);
    if (createImage) {
        QImage image = BarcodeGenerator::generateCode39(product.barcode,&error);
        QSaveFile file(path);
        if (image.isNull() || !QDir().mkpath(QFileInfo(path).absolutePath()) ||
            !file.open(QIODevice::WriteOnly) || !image.save(&file,"PNG") || !file.commit()) {
            QMessageBox::warning(this,windowTitle(),error.isEmpty() ? "Cannot save barcode PNG: " + path : error); return;
        }
    }
    if (!updated.save(catalog_.filePath(),&error)) {
        if (createImage && !fileExisted) QFile::remove(path);
        QMessageBox::warning(this,windowTitle(),error); return;
    }
    catalog_ = updated;
    accept();
}

}
