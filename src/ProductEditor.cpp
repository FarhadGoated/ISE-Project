// ProductEditor.cpp
#include "ProductEditor.h"

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

// ---- Barcode image (Code 39) ------------------------------------------------
// Each character is 9 elements: bar, space, bar, space, ... ('n' narrow, 'w' wide).
const char* codePattern(QChar c)
{
    static const char* digits[10] = {
        "nnnwwnwnn", "wnnwnnnnw", "nnwwnnnnw", "wnwwnnnnn", "nnnwwnnnw",
        "wnnwwnnnn", "nnwwwnnnn", "nnnwnnwnw", "wnnwnnwnn", "nnwwnnwnn"};
    static const char* star = "nwnnwnwnn";          // start / stop symbol
    if (c == QLatin1Char('*')) return star;
    return digits[c.unicode() - '0'];
}

// Draws "*123456*" as black bars on white and saves it as a PNG.
bool saveBarcodeImage(const QString& digits, const QString& path, QString* error)
{
    const int narrow = 2, wide = 5, quiet = 20, barHeight = 80, textHeight = 22;
    const QString text = QStringLiteral("*") + digits + QStringLiteral("*");

    int width = 2 * quiet;
    for (const QChar c : text) {
        for (const char* p = codePattern(c); *p; ++p) width += (*p == 'w') ? wide : narrow;
        width += narrow;                                   // gap between characters
    }

    QImage image(width, barHeight + textHeight, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    int x = quiet;
    for (const QChar c : text) {
        bool isBar = true;
        for (const char* p = codePattern(c); *p; ++p) {
            const int w = (*p == 'w') ? wide : narrow;
            if (isBar) painter.fillRect(x, 0, w, barHeight, Qt::black);
            x += w;
            isBar = !isBar;
        }
        x += narrow;
    }
    painter.setPen(Qt::black);
    painter.drawText(QRect(0, barHeight, width, textHeight), Qt::AlignCenter, digits);
    painter.end();

    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !image.save(path, "PNG")) {
        if (error) *error = QStringLiteral("Cannot write barcode image %1").arg(path);
        return false;
    }
    return true;
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
    barcodeLabel_ = new QLabel;

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
    form->addRow(QStringLiteral("Barcode"), barcodeLabel_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(newCategoryButton, &QPushButton::clicked, this, &ProductEditor::onNewCategory);
    connect(buttons, &QDialogButtonBox::accepted, this, &ProductEditor::onSave);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (editing_) {
        fillCategoryCombo(original_.categoryId);
        categoryBox_->setEnabled(false);          // category is part of the printed barcode
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
        barcodeLabel_->setText(original_.barcode);
    } else {
        fillCategoryCombo(QString());
        discountEdit_->setText(QStringLiteral("0.00"));
        taxEdit_->setText(QStringLiteral("0.00"));
        quantityEdit_->setText(QStringLiteral("0"));
        barcodeLabel_->setText(QStringLiteral("(generated when you save)"));
    }
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
    CategoryCatalog updated = categories_;
    QString error;
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

    // Change a copy first; the real catalog is replaced only after the file is saved.
    ProductCatalog updated = catalog_;
    bool ok = false;

    if (editing_) {
        product.barcode = original_.barcode;
        product.categoryId = original_.categoryId;
        product.barcodePath = original_.barcodePath;
        ok = updated.updateProduct(product, &error);
    } else {
        product.categoryId = categoryBox_->currentData().toString();
        if (product.categoryId.isEmpty()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Create a category first (New category...)."));
            return;
        }
        product.barcode = updated.nextBarcode(product.categoryId);
        if (product.barcode.isEmpty()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("This category has no free barcode numbers left."));
            return;
        }
        product.barcodePath = QStringLiteral("barcodes/%1.png").arg(product.barcode);

        ok = updated.addProduct(product, &error)
             && saveBarcodeImage(product.barcode, updated.resolveBarcodePath(product), &error);
    }

    if (ok) ok = updated.save(updated.filePath(), &error);
    if (!ok) {
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }

    catalog_ = updated;
    accept();
}

}
