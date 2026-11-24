#include "MainWindow.h"

#include "BarcodeFileIO.h"
#include "BarcodeGenerator.h"
#include "BarcodeReader.h"
#include "BarcodeTypes.h"
#include "BillCalculator.h"
#include "CheckoutStorage.h"
#include "ImageProcessor.h"
#include "InputValidator.h"
#include "ProductCatalog.h"
#include "ScanDialog.h"
#include "ManagerWindow.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace shop {

    namespace {

        QString findDataDirectory()
        {
            for (const QString& start : { QDir::currentPath(),
                                        QCoreApplication::applicationDirPath() }) {
                QDir folder(start);
                do {
                    if (QFileInfo(folder.filePath(QStringLiteral("data/Product.txt"))).isFile()) {
                        return folder.absoluteFilePath(QStringLiteral("data"));
                    }
                } while (folder.cdUp());
            }
            return {};
        }

        QString formatScaled(qint64 value, qint64 scale, int decimals)
        {
            const QString sign = value < 0 ? QStringLiteral("-") : QString{};
            return sign + QString::number(qAbs(value / scale)) + QLatin1Char('.')
                + QString::number(qAbs(value % scale))
                .rightJustified(decimals, QLatin1Char('0'));
        }

        QString money(qint64 paisa)
        {
            return QStringLiteral("PKR %1").arg(formatScaled(paisa, 100, 2));
        }

        QString quantityText(const Product& product, qint64 milli)
        {
            if (product.unit == Unit::Count) {
                return QString::number(milli / 1000);
            }
            return QStringLiteral("%1 kg").arg(formatScaled(milli, 1000, 3));
        }

        void showProduct(QWidget* parent, const Product& product, const QString& dataDir)
        {
            QDialog dialog(parent);
            dialog.setWindowTitle(QStringLiteral("Product Description"));
            dialog.resize(740, 350);
            dialog.setStyleSheet(QStringLiteral(
                "QLabel[productMetric=\"true\"] { font-size: 23px; font-weight: 700; }"));
            auto* layout = new QVBoxLayout(&dialog);
            layout->setContentsMargins(20, 18, 20, 16);
            layout->setSpacing(14);

            auto* identity = new QVBoxLayout;
            identity->setSpacing(4);
            auto* name = new QLabel(&dialog);
            name->setObjectName(QStringLiteral("pageTitle"));
            name->setTextFormat(Qt::PlainText);
            name->setText(product.name);
            name->setWordWrap(true);
            auto* reference = new QLabel(&dialog);
            reference->setTextFormat(Qt::PlainText);
            reference->setText(QStringLiteral("Product ID %1  \u00b7  Acronym %2").arg(product.id, product.acronym));
            reference->setProperty("muted", true);
            reference->setWordWrap(true);
            identity->addWidget(name);
            identity->addWidget(reference);
            layout->addLayout(identity);

            auto* body = new QHBoxLayout;
            body->setSpacing(14);
            auto* productDetails = new QVBoxLayout;
            productDetails->setSpacing(10);
            auto* metrics = new QHBoxLayout;
            metrics->setSpacing(10);
            const auto addMetric = [&](const QString& label, const QString& value) {
                auto* card = new QFrame(&dialog);
                card->setProperty("panel", true);
                auto* cardLayout = new QVBoxLayout(card);
                cardLayout->setContentsMargins(13, 10, 13, 10);
                cardLayout->setSpacing(4);
                auto* caption = new QLabel(label, card);
                caption->setProperty("muted", true);
                auto* amount = new QLabel(value, card);
                amount->setProperty("productMetric", true);
                amount->setWordWrap(true);
                cardLayout->addWidget(caption);
                cardLayout->addWidget(amount);
                metrics->addWidget(card, 1);
                };
            const bool counted = product.unit == Unit::Count;
            const QString stock = quantityText(product, product.stockMilli);
            addMetric(counted ? QStringLiteral("Price per unit") : QStringLiteral("Price per kg"),
                money(product.pricePaisa));
            addMetric(QStringLiteral("Available quantity"), counted
                ? QStringLiteral("%1 %2").arg(stock,
                    product.stockMilli == 1000 ? QStringLiteral("unit") : QStringLiteral("units"))
                : stock);
            productDetails->addLayout(metrics);

            auto* detailsPanel = new QFrame(&dialog);
            detailsPanel->setProperty("panel", true);
            auto* details = new QFormLayout(detailsPanel);
            details->setContentsMargins(13, 11, 13, 11);
            details->setVerticalSpacing(8);
            const auto addDetail = [&](const QString& label, const QString& value) {
                auto* caption = new QLabel(label, detailsPanel);
                caption->setProperty("muted", true);
                auto* field = new QLabel(value, detailsPanel);
                field->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                details->addRow(caption, field);
                };
            addDetail(QStringLiteral("Measurement unit"),
                counted ? QStringLiteral("Count") : QStringLiteral("Weight (kg)"));
            addDetail(counted ? QStringLiteral("Tax per unit") : QStringLiteral("Tax per kg"),
                money(product.taxPaisa));
            addDetail(counted ? QStringLiteral("Discount per unit") : QStringLiteral("Discount per kg"),
                money(product.discountPaisa));
            productDetails->addWidget(detailsPanel, 1);
            body->addLayout(productDetails, 3);

            auto* barcodePanel = new QFrame(&dialog);
            barcodePanel->setProperty("panel", true);
            auto* barcodeLayout = new QVBoxLayout(barcodePanel);
            barcodeLayout->setContentsMargins(13, 11, 13, 11);
            barcodeLayout->setSpacing(8);
            auto* barcodeHeader = new QHBoxLayout;
            auto* barcodeTitle = new QLabel(QStringLiteral("Barcode"), barcodePanel);
            barcodeTitle->setProperty("heading", true);
            auto* digits = new QLabel(barcodePanel);
            digits->setTextFormat(Qt::PlainText);
            digits->setText(product.barcode);
            digits->setTextInteractionFlags(Qt::TextSelectableByMouse);
            barcodeHeader->addWidget(barcodeTitle);
            barcodeHeader->addStretch();
            barcodeHeader->addWidget(digits);
            barcodeLayout->addLayout(barcodeHeader);

            auto* preview = new QLabel(barcodePanel);
            preview->setObjectName(QStringLiteral("barcodePreview"));
            preview->setAlignment(Qt::AlignCenter);
            preview->setMinimumSize(220, 150);
            const QPixmap barcode(QDir(dataDir).absoluteFilePath(product.barcodePath));
            if (barcode.isNull()) {
                preview->setText(QStringLiteral("Barcode image unavailable"));
            }
            else {
                preview->setPixmap(barcode.scaled(barcode.size().boundedTo(QSize(236, 144)),
                    Qt::KeepAspectRatio));
            }
            barcodeLayout->addWidget(preview, 1);
            if (!product.barcodePath.isEmpty()) {
                auto* path = new QLabel(barcodePanel);
                path->setTextFormat(Qt::PlainText);
                path->setText(product.barcodePath);
                path->setAccessibleName(QStringLiteral("Barcode image path"));
                path->setProperty("muted", true);
                path->setWordWrap(true);
                path->setTextInteractionFlags(Qt::TextSelectableByMouse);
                barcodeLayout->addWidget(path);
            }
            body->addWidget(barcodePanel, 2);
            layout->addLayout(body, 1);

            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
            layout->addWidget(buttons);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            dialog.exec();
        }

        bool lookupProduct(QWidget* parent, const QString& dataDir, Product& product,
            QString barcode = {})
        {
            ProductCatalog catalog;
            QString error;
            const QString path = QDir(dataDir).filePath(QStringLiteral("Product.txt"));
            if (!catalog.load(path, &error)) {
                QMessageBox::warning(parent, QStringLiteral("Product Catalog"),
                    error.isEmpty() ? QStringLiteral("Could not load %1.").arg(path) : error);
                return false;
            }

            if (barcode.isEmpty()) {
                barcode = scanProductBarcode(parent);
                if (barcode.isEmpty()) {
                    return false;
                }
            }
            if (!catalog.findByBarcode(barcode, product)) {
                QMessageBox::warning(parent, QStringLiteral("Product Lookup"),
                    QStringLiteral("No product found for barcode %1.").arg(barcode));
                return false;
            }
            return true;
        }

        qint64 requestQuantity(QWidget* parent, const Product& product, qint64 currentMilli)
        {
            bool accepted = false;
            const bool counted = product.unit == Unit::Count;
            const double quantity = QInputDialog::getDouble(parent,
                QStringLiteral("Quantity"),
                counted ? QStringLiteral("Quantity (units)") : QStringLiteral("Quantity (kg)"),
                currentMilli / 1000.0, counted ? 1.0 : 0.001, 1000000000.0,
                counted ? 0 : 3, &accepted);
            return accepted ? std::llround(quantity * 1000.0) : 0;
        }

        QTableWidget* makeTable(QWidget* parent, const QStringList& columns)
        {
            auto* table = new QTableWidget(0, static_cast<int>(columns.size()), parent);
            table->setHorizontalHeaderLabels(columns);
            table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
            table->horizontalHeader()->setStretchLastSection(true);
            table->verticalHeader()->hide();
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            table->setSelectionBehavior(QAbstractItemView::SelectRows);
            table->setSelectionMode(QAbstractItemView::SingleSelection);
            return table;
        }

        void addTableRow(QTableWidget* table, const QStringList& cells)
        {
            const int row = table->rowCount();
            table->insertRow(row);
            for (int column = 0; column < cells.size(); ++column) {
                table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
            }
        }

        bool collectPayment(QWidget* parent, const Bill& bill, CheckoutDetails& details)
        {
            QDialog dialog(parent);
            dialog.setWindowTitle(QStringLiteral("Checkout"));
            auto* form = new QFormLayout(&dialog);
            auto* name = new QLineEdit(&dialog);
            auto* contact = new QLineEdit(&dialog);
            auto* mode = new QComboBox(&dialog);
            mode->addItem(QStringLiteral("Cash"), QStringLiteral("cash"));
            mode->addItem(QStringLiteral("Card"), QStringLiteral("card"));
            mode->addItem(QStringLiteral("Wallet"), QStringLiteral("wallet"));
            auto* merchant = new QLineEdit(&dialog);
            merchant->setEnabled(false);

            form->addRow(QStringLiteral("Amount due"), new QLabel(money(bill.totals.payablePaisa), &dialog));
            form->addRow(QStringLiteral("Customer name"), name);
            form->addRow(QStringLiteral("Contact (optional)"), contact);
            form->addRow(QStringLiteral("Payment mode"), mode);
            form->addRow(QStringLiteral("Payment merchant"), merchant);

            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Save Bill"));
            form->addRow(buttons);
            const auto cashSelected = [mode] {
                return mode->currentData().toString() == QStringLiteral("cash");
                };
            QObject::connect(mode, &QComboBox::currentIndexChanged, &dialog, [=](int) {
                merchant->setEnabled(!cashSelected());
                });
            QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
                if (name->text().trimmed().isEmpty()) {
                    QMessageBox::warning(&dialog, QStringLiteral("Checkout"),
                        QStringLiteral("Enter the customer name."));
                    name->setFocus();
                    return;
                }
                if (!cashSelected() && merchant->text().trimmed().isEmpty()) {
                    QMessageBox::warning(&dialog, QStringLiteral("Checkout"),
                        QStringLiteral("Enter the payment merchant."));
                    merchant->setFocus();
                    return;
                }

                details.customerName = name->text().trimmed();
                details.customerContact = contact->text().trimmed();
                details.paymentMode = mode->currentData().toString();
                details.merchantName = cashSelected() ? QString{} : merchant->text().trimmed();
                details.paymentPaisa = bill.totals.payablePaisa;
                dialog.accept();
                });
            return dialog.exec() == QDialog::Accepted;
        }

        void showBill(QWidget* parent, const Bill& bill, const CheckoutDetails& details,
            const CheckoutReceipt& receipt)
        {
            QDialog dialog(parent);
            dialog.setWindowTitle(QStringLiteral("Bill %1").arg(receipt.billId));
            dialog.resize(920, 450);
            auto* layout = new QVBoxLayout(&dialog);
            auto* summary = new QLabel(&dialog);
            summary->setTextFormat(Qt::PlainText);
            summary->setText(QStringLiteral("Order %1\nCustomer %2\nPayment %3 (%4)")
                .arg(receipt.orderId, details.customerName, details.paymentMode, details.status));
            layout->addWidget(summary);

            auto* table = makeTable(&dialog, {
                QStringLiteral("Product"), QStringLiteral("Qty"), QStringLiteral("Unit price"),
                QStringLiteral("Unit discount"), QStringLiteral("Subtotal"), QStringLiteral("Tax"),
                QStringLiteral("Discount"), QStringLiteral("Payable")
                });
            for (const BillLine& line : bill.lines) {
                const auto& product = line.item.product;
                addTableRow(table, { product.acronym, quantityText(product, line.item.quantityMilli),
                    money(product.pricePaisa), money(product.discountPaisa),
                    money(line.amounts.subtotalPaisa), money(line.amounts.taxPaisa),
                    money(line.amounts.discountPaisa), money(line.amounts.payablePaisa) });
            }
            layout->addWidget(table);

            auto* totals = new QFormLayout;
            totals->addRow(QStringLiteral("Subtotal without tax"), new QLabel(money(bill.totals.subtotalPaisa), &dialog));
            totals->addRow(QStringLiteral("Total tax"), new QLabel(money(bill.totals.taxPaisa), &dialog));
            totals->addRow(QStringLiteral("Total discount"), new QLabel(money(bill.totals.discountPaisa), &dialog));
            totals->addRow(QStringLiteral("Total payable"), new QLabel(money(bill.totals.payablePaisa), &dialog));
            layout->addLayout(totals);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
            layout->addWidget(buttons);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            dialog.exec();
        }

        QString heldOrderPath(const QString& dataDir)
        {
            return QDir(dataDir).filePath(QStringLiteral("HeldOrder.json"));
        }

        bool holdOrder(const QString& path, const Order& order, QString& error)
        {
            if (QFileInfo::exists(path)) {
                error = QStringLiteral("Resume the existing held order before holding another.");
                return false;
            }

            QJsonArray items;
            for (const OrderItem& item : order.items()) {
                items.append(QJsonObject{
                    { QStringLiteral("id"), item.product.id },
                    { QStringLiteral("barcode"), item.product.barcode },
                    { QStringLiteral("quantityMilli"), QString::number(item.quantityMilli) }
                    });
            }
            const QJsonDocument document(QJsonObject{
                { QStringLiteral("status"), QStringLiteral("on-hold") },
                { QStringLiteral("items"), items }
                });
            const QByteArray bytes = document.toJson();
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
                error = file.errorString();
                return false;
            }
            return true;
        }

        bool resumeOrder(const QString& dataDir, Order& order, QString& error)
        {
            QFile file(heldOrderPath(dataDir));
            if (!file.open(QIODevice::ReadOnly)) {
                error = file.errorString();
                return false;
            }
            const QJsonObject draft = QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            const QJsonArray items = draft.value(QStringLiteral("items")).toArray();
            if (draft.value(QStringLiteral("status")).toString() != QStringLiteral("on-hold")
                || items.isEmpty()) {
                error = QStringLiteral("The held order file is invalid.");
                return false;
            }

            ProductCatalog catalog;
            if (!catalog.load(QDir(dataDir).filePath(QStringLiteral("Product.txt")), &error)) {
                return false;
            }
            Order restored;
            for (const QJsonValue& value : items) {
                const QJsonObject item = value.toObject();
                const QString barcode = item.value(QStringLiteral("barcode")).toString();
                bool quantityOk = false;
                const qint64 quantity = item.value(QStringLiteral("quantityMilli"))
                    .toString().toLongLong(&quantityOk);
                Product product;
                if (!quantityOk || !catalog.findByBarcode(barcode, product)
                    || product.id != item.value(QStringLiteral("id")).toString()) {
                    error = QStringLiteral("Could not restore held product %1.").arg(barcode);
                    return false;
                }
                if (!restored.add(product, quantity, &error)) {
                    return false;
                }
            }

            if (!file.remove()) {
                error = file.errorString();
                return false;
            }
            order = restored;
            return true;
        }

        void recordOrder(QWidget* parent, const QString& dataDir, QPushButton* startOrderButton,
            bool resumeHeld = false)
        {
            Order order;
            if (resumeHeld) {
                QString error;
                if (!resumeOrder(dataDir, order, error)) {
                    QMessageBox::warning(parent, QStringLiteral("Resume Held Order"), error);
                    return;
                }
            }
            QDialog dialog(parent);
            const QString title = resumeHeld ? QStringLiteral("Resume Held Order") : QStringLiteral("New Order");
            dialog.setWindowTitle(title);
            dialog.resize(960, 560);
            auto* layout = new QVBoxLayout(&dialog);
            layout->setContentsMargins(24, 20, 24, 20);
            layout->setSpacing(14);
            auto* heading = new QLabel(title, &dialog);
            heading->setObjectName(QStringLiteral("pageTitle"));
            layout->addWidget(heading);

            auto* scanPanel = new QFrame(&dialog);
            scanPanel->setProperty("panel", true);
            auto* controls = new QHBoxLayout(scanPanel);
            controls->setContentsMargins(14, 12, 14, 12);
            auto* scan = new QPushButton(QStringLiteral("Scan Product"), scanPanel);
            scan->setProperty("role", "primary");
            scan->setAutoDefault(false);
            auto* barcode = new QLineEdit(scanPanel);
            barcode->setObjectName(QStringLiteral("orderBarcode"));
            barcode->setPlaceholderText(QStringLiteral("e.g. 123456"));
            barcode->setValidator(new QRegularExpressionValidator(
                QRegularExpression(QStringLiteral("[1-9][0-9]{5}")), barcode));
            auto* barcodeLabel = new QLabel(QStringLiteral("&Enter barcode"), scanPanel);
            barcodeLabel->setBuddy(barcode);
            auto* enter = new QPushButton(QStringLiteral("Add Product"), scanPanel);
            enter->setAutoDefault(false);
            enter->setEnabled(false);
            controls->addWidget(scan);
            controls->addSpacing(12);
            controls->addWidget(barcodeLabel);
            controls->addWidget(barcode, 1);
            controls->addWidget(enter);
            layout->addWidget(scanPanel);

            auto* cartHeading = new QLabel(&dialog);
            cartHeading->setProperty("heading", true);
            layout->addWidget(cartHeading);
            auto* table = makeTable(&dialog, { QStringLiteral("Product"), QStringLiteral("Unit price"),
                QStringLiteral("Quantity"), QStringLiteral("Line total"), QStringLiteral(""),
                QStringLiteral("") });
            table->setAlternatingRowColors(true);
            table->verticalHeader()->setDefaultSectionSize(43);
            table->horizontalHeader()->setStretchLastSection(false);
            table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
            table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
            table->setColumnWidth(2, 192);
            table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
            table->setColumnWidth(4, 112);
            table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
            table->setColumnWidth(5, 87);
            layout->addWidget(table, 1);

            auto* checkoutPanel = new QFrame(&dialog);
            checkoutPanel->setProperty("panel", true);
            auto* checkout = new QHBoxLayout(checkoutPanel);
            checkout->setContentsMargins(14, 12, 14, 12);
            auto* close = new QPushButton(QStringLiteral("Close"), checkoutPanel);
            auto* hold = new QPushButton(QStringLiteral("Hold Order"), checkoutPanel);
            auto* generate = new QPushButton(QStringLiteral("Generate Bill"), checkoutPanel);
            generate->setProperty("role", "primary");
            for (auto* button : { close, hold, generate }) {
                button->setAutoDefault(false);
                checkout->addWidget(button);
            }
            checkout->addStretch();
            auto* totals = new QGridLayout;
            totals->setHorizontalSpacing(16);
            auto* subtotal = new QLabel(checkoutPanel);
            auto* tax = new QLabel(checkoutPanel);
            auto* discount = new QLabel(checkoutPanel);
            auto* payable = new QLabel(checkoutPanel);
            payable->setObjectName(QStringLiteral("payableAmount"));
            const auto addTotal = [&](int row, const QString& label, QLabel* amount) {
                auto* caption = new QLabel(label, checkoutPanel);
                caption->setProperty("muted", true);
                amount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                totals->addWidget(caption, row, 0);
                totals->addWidget(amount, row, 1);
                };
            addTotal(0, QStringLiteral("Subtotal"), subtotal);
            addTotal(1, QStringLiteral("Tax"), tax);
            addTotal(2, QStringLiteral("Discount"), discount);
            addTotal(3, QStringLiteral("Estimated payable"), payable);
            checkout->addLayout(totals);
            layout->addWidget(checkoutPanel);

            const auto refreshTotals = [&] {
                Bill estimate;
                const bool hasEstimate = !order.items().isEmpty()
                    && BillCalculator::calculate(order, estimate);
                const auto displayAmount = [&](qint64 amount) {
                    return hasEstimate || order.items().isEmpty() ? money(amount) : QStringLiteral("\u2014");
                    };
                for (int row = 0; row < table->rowCount(); ++row) {
                    table->item(row, 3)->setText(hasEstimate
                        ? money(estimate.lines.at(row).amounts.payablePaisa) : QStringLiteral("\u2014"));
                }
                subtotal->setText(displayAmount(estimate.totals.subtotalPaisa));
                tax->setText(displayAmount(estimate.totals.taxPaisa));
                discount->setText(displayAmount(estimate.totals.discountPaisa));
                payable->setText(displayAmount(estimate.totals.payablePaisa));
                };

            std::function<void()> refresh;
            refresh = [&] {
                table->setRowCount(0);
                for (const OrderItem& item : order.items()) {
                    const int row = table->rowCount();
                    addTableRow(table, { item.product.name, money(item.product.pricePaisa),{},{} });
                    table->item(row, 0)->setToolTip(item.product.barcode);

                    auto* quantityControls = new QWidget(table);
                    auto* quantityLayout = new QHBoxLayout(quantityControls);
                    quantityLayout->setContentsMargins(3, 0, 3, 0);
                    quantityLayout->setSpacing(0);
                    auto* less = new QPushButton(QStringLiteral("\u2212"), quantityControls);
                    auto* quantity = new QPushButton(quantityControls);
                    auto* more = new QPushButton(QStringLiteral("+"), quantityControls);
                    for (auto* button : { less, quantity, more }) {
                        button->setProperty("role", "row");
                        button->setAutoDefault(false);
                    }
                    less->setFixedWidth(36);
                    more->setFixedWidth(36);
                    less->setAccessibleName(QStringLiteral("Decrease quantity for %1").arg(item.product.name));
                    quantity->setAccessibleName(QStringLiteral("Edit quantity for %1").arg(item.product.name));
                    more->setAccessibleName(QStringLiteral("Increase quantity for %1").arg(item.product.name));
                    const qint64 step = item.product.unit == Unit::Count ? 1000 : 100;
                    const QString stepText = quantityText(item.product, step);
                    less->setToolTip(QStringLiteral("Subtract %1").arg(stepText));
                    more->setToolTip(QStringLiteral("Add %1").arg(stepText));
                    quantityLayout->addWidget(less);
                    quantityLayout->addWidget(quantity, 1);
                    quantityLayout->addWidget(more);
                    table->setCellWidget(row, 2, quantityControls);

                    const auto updateQuantityButtons = [less, quantity, more, step](const OrderItem& line) {
                        quantity->setText(quantityText(line.product, line.quantityMilli));
                        less->setEnabled(line.quantityMilli > step);
                        more->setEnabled(line.quantityMilli <= std::numeric_limits<qint64>::max() - step);
                        };
                    updateQuantityButtons(item);
                    const auto setQuantity = [&, row, updateQuantityButtons](qint64 amount) {
                        const auto& line = order.items().at(row);
                        QString error;
                        if (!order.setQuantity(line.product.id, amount, &error)) {
                            QMessageBox::warning(&dialog, QStringLiteral("Edit Quantity"), error);
                            return;
                        }
                        updateQuantityButtons(order.items().at(row));
                        refreshTotals();
                        };
                    QObject::connect(less, &QPushButton::clicked, &dialog, [&, row, step, setQuantity] {
                        setQuantity(order.items().at(row).quantityMilli - step);
                        });
                    QObject::connect(more, &QPushButton::clicked, &dialog, [&, row, step, setQuantity] {
                        setQuantity(order.items().at(row).quantityMilli + step);
                        });
                    QObject::connect(quantity, &QPushButton::clicked, &dialog, [&, row, setQuantity] {
                        const auto& line = order.items().at(row);
                        const qint64 amount = requestQuantity(&dialog, line.product, line.quantityMilli);
                        if (amount > 0) {
                            setQuantity(amount);
                        }
                        });

                    auto* description = new QPushButton(QStringLiteral("Description"), table);
                    description->setProperty("role", "row");
                    description->setAutoDefault(false);
                    table->setCellWidget(row, 4, description);
                    QObject::connect(description, &QPushButton::clicked, &dialog, [&, product = item.product] {
                        showProduct(&dialog, product, dataDir);
                        });

                    auto* remove = new QPushButton(QStringLiteral("Remove"), table);
                    remove->setProperty("role", "remove");
                    remove->setAutoDefault(false);
                    table->setCellWidget(row, 5, remove);
                    QObject::connect(remove, &QPushButton::clicked, &dialog, [&, id = item.product.id] {
                        order.remove(id);
                        refresh();
                        });
                }
                cartHeading->setText(QStringLiteral("Cart (%1)").arg(table->rowCount()));
                refreshTotals();
                generate->setEnabled(startOrderButton->isEnabled() && !order.items().isEmpty());
                hold->setEnabled(!order.items().isEmpty());
                };
            QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
            const auto addProduct = [&](QWidget* promptParent, const QString& digits) {
                Product product;
                if (!lookupProduct(promptParent, dataDir, product, digits)) {
                    return false;
                }
                const qint64 quantity = product.unit == Unit::Count
                    ? 1000 : requestQuantity(promptParent, product, 1000);
                if (quantity == 0) {
                    return false;
                }

                QString error;
                if (!order.add(product, quantity, &error)) {
                    QMessageBox::warning(promptParent, QStringLiteral("Add Product"), error);
                    return false;
                }
                refresh();
                return true;
                };
            QObject::connect(scan, &QPushButton::clicked, &dialog, [&] {
                scanOrderBarcodes(&dialog, [&](QWidget* scanner, const QString& digits) {
                    return addProduct(scanner, digits);
                    });
                barcode->setFocus();
                });
            QObject::connect(barcode, &QLineEdit::textChanged, &dialog, [&] {
                enter->setEnabled(barcode->hasAcceptableInput());
                });
            const auto addManualBarcode = [&] {
                if (barcode->hasAcceptableInput() && addProduct(&dialog, barcode->text())) {
                    barcode->clear();
                }
                barcode->setFocus();
                };
            QObject::connect(enter, &QPushButton::clicked, &dialog, addManualBarcode);
            QObject::connect(barcode, &QLineEdit::returnPressed, &dialog, addManualBarcode);
            QObject::connect(hold, &QPushButton::clicked, &dialog, [&] {
                QString error;
                if (!holdOrder(heldOrderPath(dataDir), order, error)) {
                    QMessageBox::warning(&dialog, QStringLiteral("Hold Order"), error);
                    return;
                }
                dialog.accept();
                });
            QObject::connect(generate, &QPushButton::clicked, &dialog, [&] {
                Bill bill;
                QString error;
                if (!BillCalculator::calculate(order, bill, &error)) {
                    QMessageBox::warning(&dialog, QStringLiteral("Generate Bill"), error);
                    return;
                }
                CheckoutDetails details;
                if (!collectPayment(&dialog, bill, details)) {
                    return;
                }

                generate->setEnabled(false);
                CheckoutReceipt receipt;
                if (!CheckoutStorage::save(dataDir, bill, details, QDateTime::currentDateTime(),
                    receipt, &error)) {
                    if (receipt.recoveryRequired) {
                        startOrderButton->setEnabled(false);
                        error += QStringLiteral("\nCheckout is locked for this session. Check the saved files before restarting.");
                    }
                    QMessageBox::warning(&dialog, QStringLiteral("Save Bill"), error);
                    generate->setEnabled(!receipt.recoveryRequired);
                    return;
                }
                order = Order{};
                refresh();
                showBill(&dialog, bill, details, receipt);
                });

            refresh();
            barcode->setFocus();
            dialog.exec();
        }

    } // namespace

    MainWindow::MainWindow(QWidget* parent)
        : QMainWindow(parent)
    {
        setWindowTitle(QStringLiteral("Super Duper Ultra Mega Store"));
        resize(1000, 375);

        auto* content = new QWidget(this);
        content->setObjectName(QStringLiteral("posHome"));
        setCentralWidget(content);
        auto* layout = new QVBoxLayout(content);
        layout->setContentsMargins(26, 24, 26, 24);
        layout->setSpacing(16);

        auto* header = new QHBoxLayout;
        auto* title = new QLabel(QStringLiteral("Super Duper Ultra Mega Store"), content);
        title->setObjectName(QStringLiteral("pageTitle"));
        auto* role = new QLabel(QStringLiteral("Cashier"), content);
        role->setProperty("muted", true);
        header->addWidget(title, 1);
        header->addWidget(role);
        layout->addLayout(header);

        auto* workArea = new QHBoxLayout;
        workArea->setSpacing(14);
        auto* cashier = new QGridLayout;
        cashier->setSpacing(10);
        const auto action = [content, cashier](const QString& label, int row, int column) {
            auto* button = new QPushButton(label, content);
            button->setProperty("role", "tile");
            button->setMinimumHeight(72);
            button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
            cashier->addWidget(button, row, column);
            return button;
            };
        auto* orderButton = action(QStringLiteral("New Order"), 0, 0);
        orderButton->setObjectName(QStringLiteral("newOrder"));
        auto* resumeButton = action(QStringLiteral("Resume Held Order"), 0, 1);
        auto* findOrder = action(QStringLiteral("Find Order"), 1, 0);
        findOrder->setEnabled(false);
        findOrder->setToolTip(QStringLiteral("Order history is not available yet."));
        auto* lookupButton = action(QStringLiteral("Product Lookup"), 1, 1);
        lookupButton->setObjectName(QStringLiteral("productLookup"));
        cashier->setColumnStretch(0, 1);
        cashier->setColumnStretch(1, 1);
        workArea->addLayout(cashier, 3);

        auto* managerPanel = new QFrame(content);
        managerPanel->setProperty("panel", true);
        auto* managerLayout = new QVBoxLayout(managerPanel);
        managerLayout->setContentsMargins(16, 14, 16, 14);
        auto* managerTitle = new QLabel(QStringLiteral("Manager Access"), managerPanel);
        managerTitle->setProperty("heading", true);
        auto* managerDetails = new QLabel(QStringLiteral("PIN required"), managerPanel);
        managerDetails->setProperty("muted", true);
        auto* managerButton = new QPushButton(QStringLiteral("Open Manager Panel"), managerPanel);
        managerLayout->addWidget(managerTitle);
        managerLayout->addWidget(managerDetails);
        managerLayout->addStretch();
        managerLayout->addWidget(managerButton);
        workArea->addWidget(managerPanel, 1);
        layout->addLayout(workArea);

        auto* utilityPanel = new QFrame(content);
        utilityPanel->setProperty("panel", true);
        auto* utilityLayout = new QVBoxLayout(utilityPanel);
        utilityLayout->setContentsMargins(15, 12, 15, 12);
        auto* utilityHeader = new QHBoxLayout;
        auto* toolsTitle = new QLabel(QStringLiteral("Barcode Tools"), utilityPanel);
        toolsTitle->setProperty("heading", true);
        auto* toolsToggle = new QPushButton(QStringLiteral("Show Tools"), utilityPanel);
        toolsToggle->setCheckable(true);
        utilityHeader->addWidget(toolsTitle, 1);
        utilityHeader->addWidget(toolsToggle);
        utilityLayout->addLayout(utilityHeader);
        layout->addWidget(utilityPanel);

        auto* tools = new QWidget(utilityPanel);
        auto* toolsRow = new QHBoxLayout(tools);
        toolsRow->setContentsMargins(0, 6, 0, 0);
        toolsRow->setSpacing(16);
        auto* fields = new QVBoxLayout;
        auto* inputLabel = new QLabel(QStringLiteral("Six-digit number"), tools);
        auto* input = new QLineEdit(tools);
        inputLabel->setBuddy(input);
        auto* generateButton = new QPushButton(QStringLiteral("Generate"), tools);
        auto* saveButton = new QPushButton(QStringLiteral("Save PNG"), tools);
        auto* readButton = new QPushButton(QStringLiteral("Read PNG"), tools);
        auto* scanButton = new QPushButton(QStringLiteral("Scan Camera"), tools);
        saveButton->setEnabled(false);
        auto* decodedLabel = new QLabel(QStringLiteral("Decoded number"), tools);
        auto* decodedOutput = new QLineEdit(tools);
        decodedOutput->setReadOnly(true);
        decodedLabel->setBuddy(decodedOutput);

        fields->addWidget(inputLabel);
        fields->addWidget(input);
        auto* generateActions = new QHBoxLayout;
        generateActions->addWidget(generateButton);
        generateActions->addWidget(saveButton);
        fields->addLayout(generateActions);
        auto* readActions = new QHBoxLayout;
        readActions->addWidget(readButton);
        readActions->addWidget(scanButton);
        fields->addLayout(readActions);
        fields->addWidget(decodedLabel);
        fields->addWidget(decodedOutput);

        auto* preview = new QLabel(QStringLiteral("Barcode preview"), tools);
        preview->setAlignment(Qt::AlignCenter);
        preview->setObjectName(QStringLiteral("barcodePreview"));
        preview->setMinimumSize(270, 156);
        toolsRow->addLayout(fields, 1);
        toolsRow->addWidget(preview, 2);
        utilityLayout->addWidget(tools);
        tools->hide();
        layout->addStretch();
        content->setFocusPolicy(Qt::ClickFocus);
        content->setFocus();
        connect(toolsToggle, &QPushButton::toggled, this, [tools, toolsToggle, input](bool expanded) {
            tools->setVisible(expanded);
            toolsToggle->setText(expanded ? QStringLiteral("Hide Tools") : QStringLiteral("Show Tools"));
            if (expanded) {
                input->setFocus();
            }
            });
        connect(input, &QLineEdit::returnPressed, generateButton, &QPushButton::click);

        const auto showBarcode = [this, preview, saveButton](const QImage& image) {
            previewImage_ = image;
            const QPixmap pixmap = QPixmap::fromImage(previewImage_);
            const QSize previewSize = preview->contentsRect().size();
            preview->setPixmap(pixmap.scaled(
                pixmap.size().boundedTo(previewSize), Qt::KeepAspectRatio));
            saveButton->setEnabled(true);
            };

        connect(generateButton, &QPushButton::clicked, this,
            [this, input, decodedOutput, showBarcode] {
                const QString digits = input->text();
                QString error;

                if (!InputValidator::isValidSixDigitNumber(digits, &error)) {
                    QMessageBox::warning(this, QStringLiteral("Input validation"), error);
                    return;
                }

                const QImage image = BarcodeGenerator::generateCode39(digits, &error);
                if (image.isNull()) {
                    QMessageBox::warning(this, QStringLiteral("Barcode generation"), error);
                    return;
                }

                decodedOutput->clear();
                showBarcode(image);
            });

        connect(saveButton, &QPushButton::clicked, this, [this] {
            QFileDialog dialog(this, QStringLiteral("Save barcode"));
            dialog.setAcceptMode(QFileDialog::AcceptSave);
            dialog.setNameFilter(QStringLiteral("PNG images (*.png)"));
            dialog.setDefaultSuffix(QStringLiteral("png"));
            dialog.selectFile(QStringLiteral("barcode.png"));

            if (dialog.exec() != QDialog::Accepted) {
                return;
            }

            const QString path = dialog.selectedFiles().constFirst();
            QString error;

            if (!BarcodeFileIO::savePng(previewImage_, path, &error)) {
                QMessageBox::warning(this, QStringLiteral("Save barcode"), error);
            }
            });

        connect(readButton, &QPushButton::clicked, this,
            [this, decodedOutput, showBarcode] {
                const QString path = QFileDialog::getOpenFileName(
                    this, QStringLiteral("Open barcode"), QString(),
                    QStringLiteral("PNG images (*.png)"));
                if (path.isEmpty()) {
                    return;
                }

                QString error;
                const QImage image = BarcodeFileIO::loadImage(path, &error);
                if (image.isNull()) {
                    QMessageBox::warning(this, QStringLiteral("Open barcode"), error);
                    return;
                }

                std::vector<Run> runs;
                if (!ImageProcessor::extractRuns(image, runs, &error)) {
                    QMessageBox::warning(this, QStringLiteral("Read barcode"), error);
                    return;
                }

                QString digits;
                if (!BarcodeReader::decodeCode39Digits(runs, digits, &error)) {
                    QMessageBox::warning(this, QStringLiteral("Read barcode"), error);
                    return;
                }

                showBarcode(image);
                decodedOutput->setText(digits);
            });

        const auto dataDirectory = [this] {
            const QString dataDir = findDataDirectory();
            if (dataDir.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Product Catalog"),
                    QStringLiteral("Could not find data/Product.txt. Place it in the project data folder."));
            }
            return dataDir;
            };
        connect(managerButton, &QPushButton::clicked, this, [this, dataDirectory] {
            const QString dataDir = dataDirectory();
            if (!dataDir.isEmpty()) {
                openManagerWindow(this, dataDir);
            }
            });
        connect(lookupButton, &QPushButton::clicked, this, [this, dataDirectory] {
            const QString dataDir = dataDirectory();
            Product product;
            if (!dataDir.isEmpty() && lookupProduct(this, dataDir, product)) {
                showProduct(this, product, dataDir);
            }
            });
        connect(orderButton, &QPushButton::clicked, this, [this, orderButton, dataDirectory] {
            const QString dataDir = dataDirectory();
            if (!dataDir.isEmpty()) {
                recordOrder(this, dataDir, orderButton);
            }
            });
        connect(resumeButton, &QPushButton::clicked, this, [this, orderButton, dataDirectory] {
            const QString dataDir = dataDirectory();
            if (dataDir.isEmpty()) {
                return;
            }
            if (!orderButton->isEnabled()) {
                QMessageBox::warning(this, QStringLiteral("Resume Held Order"),
                    QStringLiteral("Check the saved checkout files before restarting the app."));
                return;
            }
            if (!QFileInfo::exists(heldOrderPath(dataDir))) {
                QMessageBox::information(this, QStringLiteral("Resume Held Order"),
                    QStringLiteral("There is no held order to resume."));
                return;
            }
            recordOrder(this, dataDir, orderButton, true);
            });
        connect(scanButton, &QPushButton::clicked, this, [this, decodedOutput] {
            const QString digits = scanProductBarcode(this);
            if (!digits.isEmpty()) {
                decodedOutput->setText(digits);
            }
            });
    }

} // namespace shop
