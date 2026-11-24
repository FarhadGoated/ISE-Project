#include "ManagerWindow.h"

#include "ManagerAccess.h"
#include "ProductCatalog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace shop {
    namespace {

        QString askPin(QWidget* parent, const QString& title, const QString& prompt, bool& accepted)
        {
            return QInputDialog::getText(parent, title, prompt, QLineEdit::Password, {}, &accepted);
        }

        void promptPinChange(QWidget* parent)
        {
            const QString title = QStringLiteral("Change Manager PIN");
            bool accepted = false;
            const QString current = askPin(parent, title, QStringLiteral("Current PIN"), accepted);
            if (!accepted) {
                return;
            }
            const QString next = askPin(parent, title, QStringLiteral("New four-digit PIN"), accepted);
            if (!accepted) {
                return;
            }
            const QString confirmation = askPin(parent, title, QStringLiteral("Confirm new PIN"), accepted);
            if (!accepted) {
                return;
            }
            if (confirmation != next) {
                QMessageBox::warning(parent, title, QStringLiteral("The new PINs do not match."));
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
        bool accepted = false;
        const QString pin = askPin(parent, title, QStringLiteral("Manager PIN"), accepted);
        if (!accepted) {
            return;
        }
        if (!verifyManagerPin(pin)) {
            QMessageBox::warning(parent, title, QStringLiteral("Incorrect manager PIN."));
            return;
        }

        QDialog dialog(parent);
        dialog.setWindowTitle(title);
        dialog.setObjectName(QStringLiteral("managerDashboard"));
        dialog.resize(820, 500);
        dialog.setStyleSheet(QStringLiteral(R"(
        QDialog#managerDashboard { background: #121316; }
        QLabel { color: #e9eaf0; }
        QLabel#managerHeading { font-size: 25px; font-weight: 700; }
        QLabel#pageHeading { font-size: 19px; font-weight: 600; }
        QListWidget, QTableWidget {
            background: #1a1c20; color: #eeeff2; border: 1px solid #303238;
            border-radius: 7px; alternate-background-color: #202228;
        }
        QListWidget::item { padding: 9px; }
        QListWidget::item:selected { background: #3a4061; color: #f5f5ff; }
        QHeaderView::section {
            background: #282b34; color: #eeeff2; padding: 6px; border: none;
        }
        QPushButton {
            background: #242731; color: #eeeff2; border: 1px solid #4a4d57;
            padding: 7px 13px; border-radius: 7px;
        }
        QPushButton:hover { background: #343849; }
        QPushButton:focus { border-color: #a8b1ff; }
    )"));

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
