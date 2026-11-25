#include "ManagerWindow.h"
#include "MainWindow.h"

#include "ManagerAccess.h"
#include "CheckoutStorage.h"
#include "CategoryCatalog.h"
#include "ProductEditor.h"
#include "InventoryDialog.h"
#include "Reports.h"
#include "BillPdf.h"
#include <QFormLayout>
#include <QLockFile>
#include <QShowEvent>
#include "ProductCatalog.h"
#include "SeasonsDialog.h"
#include <QSizePolicy>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <functional>

namespace shop {
    namespace {

        QString askPin(QWidget* parent, const QString& title, const QString& prompt,
            const std::function<bool(const QString&)>& verify = {},
            const QString& failureMessage = {})
        {
            QDialog dialog(parent);
            dialog.setWindowTitle(title);
            dialog.setMinimumWidth(320);

            auto* layout = new QVBoxLayout(&dialog);
            layout->setContentsMargins(18, 16, 18, 16);
            layout->setSpacing(10);
            auto* label = new QLabel(prompt, &dialog);
            auto* field = new QLineEdit(&dialog);
            field->setObjectName(QStringLiteral("managerPinEdit"));
            field->setEchoMode(QLineEdit::Password);
            field->setAlignment(Qt::AlignCenter);
            field->setInputMethodHints(Qt::ImhDigitsOnly | Qt::ImhSensitiveData);
            field->setValidator(new QRegularExpressionValidator(
                QRegularExpression(QStringLiteral("[0-9]{4}")), field));
            label->setBuddy(field);

            auto* error = new QLabel(&dialog);
            error->setObjectName(QStringLiteral("managerPinError"));
            error->setWordWrap(true);
            error->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            error->setFixedHeight(2 * error->fontMetrics().lineSpacing());
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
            buttons->button(QDialogButtonBox::Cancel)->setAutoDefault(false);

            auto* keypad = new QGridLayout;
            keypad->setSpacing(6);
            for (int column = 0; column < 3; ++column) {
                keypad->setColumnStretch(column, 1);
            }
            const auto addKey = [&](const QString& text, int row, int column) {
                auto* button = new QPushButton(text, &dialog);
                button->setMinimumHeight(42);
                button->setAutoDefault(false);
                button->setFocusPolicy(Qt::TabFocus);
                keypad->addWidget(button, row, column);
                return button;
                };
            const auto addDigit = [&](int digit, int row, int column) {
                const QString text = QString::number(digit);
                QObject::connect(addKey(text, row, column), &QPushButton::clicked, field,
                    [field, text] { field->insert(text); });
                };
            for (int digit = 1; digit <= 9; ++digit) {
                addDigit(digit, (digit - 1) / 3, (digit - 1) % 3);
            }
            auto* clear = addKey(QStringLiteral("Clear"), 3, 0);
            addDigit(0, 3, 1);
            auto* backspace = addKey(QStringLiteral("\u232b"), 3, 2);
            backspace->setAccessibleName(QStringLiteral("Backspace"));
            backspace->setToolTip(QStringLiteral("Backspace"));
            QObject::connect(clear, &QPushButton::clicked, field, &QLineEdit::clear);
            QObject::connect(backspace, &QPushButton::clicked, field, &QLineEdit::backspace);

            layout->addWidget(label);
            layout->addWidget(field);
            layout->addWidget(error);
            layout->addLayout(keypad);
            layout->addWidget(buttons);

            QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            QObject::connect(field, &QLineEdit::textChanged, &dialog, [&] {
                if (!field->hasAcceptableInput()) {
                    error->clear();
                    return;
                }
                if (verify && !verify(field->text())) {
                    error->setText(failureMessage);
                    field->setFocus();
                    field->selectAll();
                    return;
                }
                dialog.accept();
                });
            field->setFocus();
            return dialog.exec() == QDialog::Accepted ? field->text() : QString{};
        }

        void promptPinChange(QWidget* parent)
        {
            const QString title = QStringLiteral("Change Manager PIN");
            const QString current = askPin(parent, title, QStringLiteral("Current four-digit PIN"),
                verifyManagerPin, QStringLiteral("Incorrect current PIN. Try again."));
            if (current.isEmpty()) {
                return;
            }
            const QString next = askPin(parent, title, QStringLiteral("New four-digit PIN"));
            if (next.isEmpty()) {
                return;
            }
            const QString confirmation = askPin(parent, title, QStringLiteral("Confirm new PIN"),
                [&next](const QString& pin) { return pin == next; },
                QStringLiteral("PINs do not match. Try again."));
            if (confirmation.isEmpty()) {
                return;
            }
            QString error;
            if (!changeManagerPin(current, next, error)) {
                QMessageBox::warning(parent, title, error);
                return;
            }
            QMessageBox::information(parent, title, QStringLiteral("Manager PIN updated."));
        }

        struct EditRequest {
            QString billId;
            bool approved = false;
        };
        // Each running till keeps manager approvals for its own requests.
        QHash<QString, QHash<QString, EditRequest>> requests;

        class StockPage final : public QWidget {
        public:
            StockPage(const QString& dir, QWidget* parent) : QWidget(parent), dir_(dir) {
                auto* layout = new QVBoxLayout(this);
                status_ = new QLabel(this);
                layout->addWidget(status_);
            }
        protected:
            void showEvent(QShowEvent* event) override {
                QWidget::showEvent(event);
                if (editor_) return;
                QString error;
                if (!catalog_.load(QDir(dir_).filePath("Product.txt"), &error)) {
                    status_->setText(error); return;
                }
                status_->hide();
                editor_ = new InventoryDialog(catalog_, this, true);
                layout()->addWidget(editor_);
            }
        private:
            QString dir_;
            ProductCatalog catalog_;
            QLabel* status_;
            InventoryDialog* editor_ = nullptr;
        };

        class CatalogPage final : public QWidget {
        public:
            CatalogPage(const QString& directory, bool products, QWidget* parent)
                : QWidget(parent), dir_(directory), products_(products)
            {
                auto* layout = new QVBoxLayout(this);
                auto* tools = new QHBoxLayout;
                auto* add = new QPushButton(products ? "Add product" : "Add category", this);
                auto* edit = new QPushButton("Edit selected", this);
                auto* remove = new QPushButton("Delete unused category", this);
                remove->setVisible(!products);
                auto* reload = new QPushButton("Refresh", this);
                for (auto* button : { add,edit,remove,reload }) tools->addWidget(button);
                layout->addLayout(tools);
                const QStringList headers = products ? QStringList{ "ID","Name","Barcode","Category","Unit price (PKR)" }
                : QStringList{ "ID","Name","Acronym" };
                table_ = new QTableWidget(0, headers.size(), this);
                table_->setHorizontalHeaderLabels(headers);
                table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
                table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
                table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
                table_->setSelectionBehavior(QAbstractItemView::SelectRows);
                table_->setSelectionMode(QAbstractItemView::SingleSelection);
                layout->addWidget(table_);
                status_ = new QLabel(this); layout->addWidget(status_);
                connect(add, &QPushButton::clicked, this, [this] { change(true, false); });
                connect(edit, &QPushButton::clicked, this, [this] { change(false, false); });
                connect(remove, &QPushButton::clicked, this, [this] { change(false, true); });
                connect(reload, &QPushButton::clicked, this, [this] { refresh(); });
                connect(table_, &QTableWidget::cellDoubleClicked, this, [this] { change(false, false); });
            }
        protected:
            void showEvent(QShowEvent* event) override { QWidget::showEvent(event); refresh(); }
        private:
            bool load(ProductCatalog& products, CategoryCatalog& categories, QString& error) {
                return products.load(dir_.filePath("Product.txt"), &error) &&
                    categories.load(dir_.filePath("PCategory.txt"), &error);
            }
            void append(const QStringList& fields) {
                int row = table_->rowCount();
                table_->insertRow(row);
                for (int i = 0; i < fields.size(); ++i) table_->setItem(row, i, new QTableWidgetItem(fields.at(i)));
            }
            void refresh() {
                table_->setRowCount(0);
                ProductCatalog products;
                CategoryCatalog categories;
                QString error;
                if (!load(products, categories, error)) { status_->setText(error); return; }
                if (products_) {
                    for (const auto& p : products.products()) append({ p.id,p.name,p.barcode,p.categoryId,formatPaisa(p.pricePaisa) });
                }
                else {
                    for (const auto& c : categories.categories()) append({ c.id,c.name,c.acronym });
                }
                status_->clear();
            }
            void change(bool adding, bool deleting) {
                if (!adding && table_->currentRow() < 0) { status_->setText("Select a row first."); return; }
                const QString selected = adding ? QString() : table_->item(table_->currentRow(), 0)->text();
                ProductCatalog products;
                CategoryCatalog categories;
                QString error;
                if (!load(products, categories, error)) { status_->setText(error); return; }
                if (products_) {
                    Product existing;
                    if (!adding && !products.findById(selected, existing)) { refresh(); return; }
                    ProductEditor editor(products, categories, this, selected);
                    editor.exec(); refresh(); return;
                }
                Category original, result;
                if (!adding && !categories.findById(selected, original)) { refresh(); return; }
                if (deleting) {
                    if (QMessageBox::question(this, "Delete category", "Delete " + original.name + "?") != QMessageBox::Yes) return;
                }
                else {
                    QDialog dialog(this);
                    dialog.setWindowTitle(adding ? "Add category" : "Edit category");
                    auto* form = new QFormLayout(&dialog);
                    auto* id = new QLineEdit(adding ? categories.nextFreeId() : selected, &dialog);
                    id->setReadOnly(!adding); id->setMaxLength(2);
                    auto* name = new QLineEdit(original.name, &dialog);
                    auto* acronym = new QLineEdit(original.acronym, &dialog); acronym->setMaxLength(8);
                    form->addRow("ID (10 to 99)", id); form->addRow("Name", name); form->addRow("Acronym", acronym);
                    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
                    form->addRow(buttons);
                    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
                    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                    if (dialog.exec() != QDialog::Accepted) return;
                    result = { id->text().trimmed(),name->text().trimmed(),acronym->text().trimmed() };
                }
                QLockFile lock(dir_.filePath(".checkout.lock"));
                if (!lock.tryLock(0)) { status_->setText("Catalog is busy. Try again."); return; }
                if (!load(products, categories, error)) { status_->setText(error); return; }
                Category latest;
                if (!adding && (!categories.findById(selected, latest) ||
                    latest.name != original.name || latest.acronym != original.acronym)) {
                    refresh(); status_->setText("The category changed. Review it again."); return;
                }
                bool ok = deleting ? categories.removeUnused(selected, products, &error) :
                    adding ? categories.add(result, &error) : categories.edit(result, &error);
                if (!ok || !categories.save(&error)) { status_->setText(error); return; }
                refresh();
            }
            QDir dir_;
            bool products_;
            QTableWidget* table_;
            QLabel* status_;
        };

        QWidget* approvalPage(const QString& dir, QWidget* parent)
        {
            auto* page = new QWidget(parent);
            auto* layout = new QVBoxLayout(page);
            auto* table = new QTableWidget(0, 3, page);
            table->setHorizontalHeaderLabels({ "Order ID", "Customer", "Approval status" });
            table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
            table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
            table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
            table->verticalHeader()->setDefaultSectionSize(38);
            table->verticalHeader()->hide();
            table->setSelectionBehavior(QAbstractItemView::SelectRows);
            table->setSelectionMode(QAbstractItemView::SingleSelection);
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            table->setAlternatingRowColors(true);
            table->setShowGrid(false);
            auto* approve = new QPushButton("Approve selected revision", page);
            auto* status = new QLabel(page);
            layout->addWidget(table, 1);
            auto* footer = new QHBoxLayout;
            footer->addWidget(status, 1);
            footer->addWidget(approve);
            layout->addLayout(footer);
            const auto refresh = [=] {
                table->setRowCount(0);
                approve->setEnabled(false);
                store::CheckoutStorage storage(dir, nullptr);
                if (!storage.load()) { status->setText(storage.loadErrors().join('\n')); return; }
                for (const auto& row : storage.ordersWithStatus(store::Status::Paid)) {
                    if (!storage.checkPaidEditWindow(row.orderId, QDateTime::currentDateTime()).ok) continue;
                    const auto request = requests.value(dir).value(row.orderId);
                    const bool current = request.billId == row.activeBillId;
                    QString state = !current ? "Eligible" : request.approved ? "Approved" : "Requested";
                    const int index = table->rowCount();
                    table->insertRow(index);
                    auto* id = new QTableWidgetItem(row.orderId);
                    id->setData(Qt::UserRole, row.activeBillId);
                    table->setItem(index, 0, id);
                    table->setItem(index, 1, new QTableWidgetItem(row.customerName));
                    table->setItem(index, 2, new QTableWidgetItem(state));
                }
                status->setText(table->rowCount() ? "Each approval covers one saved revision." : "No eligible paid orders.");
                };
            QObject::connect(table, &QTableWidget::itemSelectionChanged, page, [=] {
                approve->setEnabled(!table->selectedItems().isEmpty());
                });
            QObject::connect(approve, &QPushButton::clicked, page, [=] {
                const auto selected = table->selectedRanges();
                if (selected.isEmpty()) return;
                const auto* id = table->item(selected.constFirst().topRow(), 0);
                if (!id) return;
                requests[dir][id->text()] = { id->data(Qt::UserRole).toString(), true };
                refresh();
                });
            refresh(); return page;
        }

    } // namespace

    bool authorizeManager(QWidget* parent, const QString& action)
    {
        return !askPin(parent, action, "Enter manager PIN", verifyManagerPin, "Incorrect PIN. Try again.").isEmpty();
    }

    bool requestPaidEdit(QWidget* parent, const QString& dir, const QString& id, const QString& billId)
    {
        const auto request = requests.value(dir).value(id);
        if (request.billId == billId && request.approved) return true;
        requests[dir][id] = { billId,false };
        if (!authorizeManager(parent, "Approve edit for order " + id)) {
            QMessageBox::information(parent, "Approval requested", "The manager can approve order " + id +
                " under Paid Order Approvals. Reopen it after approval.");
            return false;
        }
        requests[dir][id].approved = true;
        return true;
    }

    void finishPaidEdit(const QString& dir, const QString& id) { requests[dir].remove(id); }

    void openManagerWindow(QWidget* parent, const QString& dataDir)
    {
        const QString title = QStringLiteral("Manager");
        const QString pin = askPin(parent, title, QStringLiteral("Enter manager PIN"),
            verifyManagerPin, QStringLiteral("Incorrect manager PIN. Try again."));
        if (pin.isEmpty()) {
            return;
        }

        QDialog dialog(parent);
        dialog.setWindowTitle(title);
        dialog.setObjectName(QStringLiteral("managerDashboard"));
        dialog.resize(1050, 620);

        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(20, 18, 20, 16);
        layout->setSpacing(12);
        auto* heading = new QLabel(QStringLiteral("Manager"), &dialog);
        heading->setObjectName(QStringLiteral("managerHeading"));
        auto* change = new QPushButton(QStringLiteral("Change Manager PIN"), &dialog);
        auto* titleRow = new QHBoxLayout;
        titleRow->addWidget(heading);
        titleRow->addStretch();
        titleRow->addWidget(change);
        layout->addLayout(titleRow);

        auto* navigation = new QListWidget(&dialog);
        navigation->setObjectName("managerNavigation");
        navigation->setAccessibleName(QStringLiteral("Manager sections"));
        navigation->setFixedWidth(188);
        auto* pages = new QStackedWidget(&dialog);
        const auto section = [&](const QString& name, QWidget* page) {
            navigation->addItem(name); pages->addWidget(page);
            };
        section("Inventory", new StockPage(dataDir, pages));
        section("Categories", new CatalogPage(dataDir, false, pages));
        section("Products", new CatalogPage(dataDir, true, pages));
        section("Seasonal Discounts", makeSeasonsPage(dataDir, pages));
        section("Reports", makeReportsPage(dataDir, pages));
        section("Paid Order Approvals", approvalPage(dataDir, pages));
        auto* lookup = new QPushButton("Product lookup", &dialog);
        titleRow->insertWidget(titleRow->count() - 1, lookup);
        QObject::connect(lookup, &QPushButton::clicked, &dialog, [&] { openProductLookup(&dialog, dataDir); });
        auto* print = new QPushButton("Reprint Bill", &dialog);
        titleRow->insertWidget(titleRow->count() - 1, print);
        QObject::connect(print, &QPushButton::clicked, &dialog, [&] { reprintBill(&dialog, dataDir); });
        auto* workspace = new QHBoxLayout;
        workspace->setSpacing(16);
        workspace->addWidget(navigation);
        auto* pageContent = new QFrame(&dialog);
        pageContent->setProperty("panel", true);
        auto* pageLayout = new QVBoxLayout(pageContent);
        pageLayout->setContentsMargins(12, 12, 12, 12);
        pageLayout->addWidget(pages);
        workspace->addWidget(pageContent, 1);
        layout->addLayout(workspace, 1);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        layout->addWidget(buttons);
        QObject::connect(navigation, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
        navigation->setCurrentRow(0);
        QObject::connect(change, &QPushButton::clicked, &dialog, [&] { promptPinChange(&dialog); });
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.exec();
    }

} // namespace shop
