#include "ManagerWindow.h"

#include "ManagerAccess.h"
#include "ProductCatalog.h"
#include <QSizePolicy>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
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

        QWidget* makeInventoryPage(const QString& dataDir, QWidget* parent)
        {
            auto* page = new QWidget(parent);
            auto* layout = new QVBoxLayout(page);
            auto* heading = new QLabel(QStringLiteral("Inventory"), page);
            heading->setObjectName(QStringLiteral("pageHeading"));
            auto* refreshButton = new QPushButton(QStringLiteral("Refresh"), page);
            auto* titleRow = new QHBoxLayout;
            titleRow->addWidget(heading);
            titleRow->addStretch();
            titleRow->addWidget(refreshButton);
            layout->addLayout(titleRow);

            auto* table = new QTableWidget(0, 4, page);
            table->setObjectName(QStringLiteral("managerInventoryTable"));
            table->setHorizontalHeaderLabels({ QStringLiteral("Product"), QStringLiteral("Barcode"),
                                              QStringLiteral("Unit"), QStringLiteral("Stock on hand") });
            table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
            table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
            table->verticalHeader()->hide();
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            table->setSelectionBehavior(QAbstractItemView::SelectRows);
            table->setAlternatingRowColors(true);
            layout->addWidget(table, 1);

            auto* status = new QLabel(page);
            status->setTextFormat(Qt::PlainText);
            status->setWordWrap(true);
            layout->addWidget(status);

            const QString path = QDir(dataDir).filePath(QStringLiteral("Product.txt"));
            const auto refresh = [table, status, path] {
                ProductCatalog catalog;
                QString error;
                table->setRowCount(0);
                if (!catalog.load(path, &error)) {
                    status->setText(error.isEmpty()
                        ? QStringLiteral("Could not load Product.txt.") : error);
                    return;
                }

                const auto products = catalog.products();
                for (const Product& product : products) {
                    const int row = table->rowCount();
                    table->insertRow(row);
                    table->setItem(row, 0, new QTableWidgetItem(product.name));
                    table->setItem(row, 1, new QTableWidgetItem(product.barcode));
                    table->setItem(row, 2, new QTableWidgetItem(
                        product.unit == Unit::Count ? QStringLiteral("Pieces") : QStringLiteral("kg")));
                    auto* quantity = new QTableWidgetItem(formatQuantityMilli(product.stockMilli, product.unit));
                    quantity->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    table->setItem(row, 3, quantity);
                }
                status->setText(QStringLiteral("%1 products").arg(products.size()));
                };
            QObject::connect(refreshButton, &QPushButton::clicked, page, refresh);
            refresh();
            return page;
        }

        void addPendingPage(QListWidget* navigation, QStackedWidget* pages,
            const QString& title, const QString& status)
        {
            navigation->addItem(title);
            auto* page = new QWidget(pages);
            auto* layout = new QVBoxLayout(page);
            auto* heading = new QLabel(title, page);
            heading->setObjectName(QStringLiteral("pageHeading"));
            auto* detail = new QLabel(status, page);
            detail->setWordWrap(true);
            layout->addWidget(heading);
            layout->addWidget(detail);
            layout->addStretch();
            pages->addWidget(page);
        }

    } // namespace

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
        dialog.resize(820, 500);

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
        navigation->setAccessibleName(QStringLiteral("Manager sections"));
        navigation->setFixedWidth(170);
        auto* pages = new QStackedWidget(&dialog);
        navigation->addItem(QStringLiteral("Inventory"));
        pages->addWidget(makeInventoryPage(dataDir, pages));
        addPendingPage(navigation, pages, QStringLiteral("Categories"),
            QStringLiteral("Category management is pending."));
        addPendingPage(navigation, pages, QStringLiteral("Products"),
            QStringLiteral("Adding and editing products is pending."));
        addPendingPage(navigation, pages, QStringLiteral("Seasonal Discounts"),
            QStringLiteral("Seasonal discount management is pending."));
        addPendingPage(navigation, pages, QStringLiteral("Reports"),
            QStringLiteral("Reports will appear here once order and bill records are connected."));
        addPendingPage(navigation, pages, QStringLiteral("Paid Order Approvals"),
            QStringLiteral("Approval requests will appear here once paid-order editing is connected."));
        auto* workspace = new QHBoxLayout;
        workspace->addWidget(navigation);
        workspace->addWidget(pages, 1);
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
