#include "MainWindow.h"

#include "BarcodeFileIO.h"
#include "BarcodeGenerator.h"
#include "BarcodeReader.h"
#include "BarcodeTypes.h"
#include "BillCalculator.h"
#include "CheckoutStorage.h"
#include "BillPdf.h"
#include "BillStorage.h"
#include "PaymentDialog.h"
#include "OrderStorage.h"
#include "OrderDialog.h"
#include <QLockFile>
#include "SeasonDiscounts.h"
#include <QTimer>
#include <QListWidget>
#include <QStatusBar>
#include "ImageProcessor.h"
#include "InputValidator.h"
#include "ProductCatalog.h"
#include "ScanDialog.h"
#include "ManagerWindow.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
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
            store::CheckoutStorage ledger(dataDir, nullptr);
            ProductCatalog live;
            Product latest;
            const bool readable = ledger.load() && live.load(QDir(dataDir).filePath("Product.txt")) &&
                live.findById(product.id, latest);
            const auto available = readable ? qMax<qint64>(0, latest.stockMilli -
                ledger.reservedStock(product.id, QDateTime::currentDateTime())) : 0;
            const QString stock = quantityText(product, available);
            addMetric(counted ? QStringLiteral("Price per unit") : QStringLiteral("Price per kg"),
                money(product.pricePaisa));
            addMetric(QStringLiteral("Available quantity"), !readable ? QStringLiteral("Unavailable") : counted
                ? QStringLiteral("%1 %2").arg(stock,
                    available == 1000 ? QStringLiteral("unit") : QStringLiteral("units"))
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

        void showBill(QWidget* parent, const Bill& bill, const CheckoutDetails& details,
            const CheckoutReceipt& receipt)
        {
            QDialog dialog(parent);
            dialog.setWindowTitle(QStringLiteral("Bill %1").arg(receipt.billId));
            dialog.setObjectName("receiptDialog");
            dialog.resize(800, 560);
            auto* layout = new QVBoxLayout(&dialog);
            layout->setContentsMargins(20, 18, 20, 18);
            layout->setSpacing(12);
            auto* receiptTitle = new QLabel("Bill " + receipt.billId, &dialog);
            receiptTitle->setObjectName("pageTitle");
            layout->addWidget(receiptTitle);
            auto* summary = new QLabel(&dialog);
            summary->setProperty("muted", true);
            summary->setTextFormat(Qt::PlainText);
            summary->setText(QStringLiteral("Super Duper Ultra Mega Store   |   %5\nOrder %1   |   Customer %2\nPayment %3 (%4)")
                .arg(receipt.orderId, details.customerName, details.paymentMode, details.status,
                    QDateTime(bill.billDate, bill.billTime).toString("yyyy-MM-dd HH:mm:ss")));
            layout->addWidget(summary);

            auto* table = makeTable(&dialog, {
                QStringLiteral("Product"), QStringLiteral("Qty"), QStringLiteral("Unit price"),
                QStringLiteral("Subtotal"), QStringLiteral("Product discount")
                });
            for (const BillLine& line : bill.lines) {
                const auto& product = line.item.product;
                addTableRow(table, { product.acronym, quantityText(product, line.item.quantityMilli),
                    money(product.pricePaisa), money(line.amounts.subtotalPaisa), money(line.amounts.discountPaisa) });
            }
            layout->addWidget(table);

            auto* totals = new QFormLayout;
            totals->addRow(QStringLiteral("Subtotal without tax"), new QLabel(money(bill.totals.subtotalPaisa), &dialog));
            totals->addRow(QStringLiteral("Total tax"), new QLabel(money(bill.totals.taxPaisa), &dialog));
            totals->addRow(QStringLiteral("Seasonal discount"), new QLabel(money(bill.seasonalDiscountPaisa), &dialog));
            const QString specialCaption = bill.specialBasisPoints > 0
                ? QStringLiteral("Special discount (%1%)").arg(
                    BillCalculator::formatPercentBasisPoints(bill.specialBasisPoints))
                : QStringLiteral("Special discount");
            totals->addRow(specialCaption, new QLabel(money(bill.specialDiscountPaisa), &dialog));
            totals->addRow(QStringLiteral("Amount received"), new QLabel(money(bill.amountReceivedPaisa), &dialog));
            totals->addRow(QStringLiteral("Balance returned"), new QLabel(money(bill.balanceReturnedPaisa), &dialog));
            totals->addRow(QStringLiteral("Product discounts"), new QLabel(money(bill.totals.discountPaisa), &dialog));
            auto* finalPayable = new QLabel(money(bill.totals.payablePaisa), &dialog);
            finalPayable->setObjectName("payableAmount");
            totals->addRow(QStringLiteral("Total payable"), finalPayable);
            layout->addLayout(totals);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
            auto* pdf = buttons->addButton(QStringLiteral("Save PDF"), QDialogButtonBox::ActionRole);
            pdf->setProperty("role", "primary");
            QObject::connect(pdf, &QPushButton::clicked, &dialog, [&] { offerBillPdf(&dialog, bill); });
            layout->addWidget(buttons);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            dialog.exec();
        }

        template<class Operation>
        bool changeQuantity(QWidget* parent, Unit unit, Operation operation, QString& error)
        {
            auto outcome = operation(AtLimitChoice::Ask);
            if (outcome == AddOutcome::NeedsDecision) {
                QMessageBox prompt(parent);
                prompt.setWindowTitle("Last stock");
                prompt.setText("This quantity uses all available stock.");
                auto* sell = prompt.addButton("Sell all", QMessageBox::AcceptRole);
                auto* keep = prompt.addButton(unit == Unit::Count ? "Keep one piece" : "Keep one gram",
                    QMessageBox::ActionRole);
                prompt.addButton(QMessageBox::Cancel);
                prompt.exec();
                if (prompt.clickedButton() != sell && prompt.clickedButton() != keep) return false;
                outcome = operation(prompt.clickedButton() == sell ? AtLimitChoice::SellAll : AtLimitChoice::KeepOne);
            }
            if (outcome != AddOutcome::Added) QMessageBox::warning(parent, "Quantity", error);
            return outcome == AddOutcome::Added;
        }

        void recordOrder(QWidget* parent, const QString& dataDir, QPushButton* startOrderButton,
            const store::OrderRecord& prior = {})
        {
            QString problem;
            const bool existing = !prior.orderId.isEmpty();
            const bool revision = existing && prior.status == store::Status::Paid;
            store::CheckoutStorage ledger(dataDir, nullptr);
            ProductCatalog catalog;
            if (!ledger.load() || !catalog.load(QDir(dataDir).filePath("Product.txt"), &problem)) {
                QMessageBox::warning(parent, "Open cart", problem.isEmpty() ? ledger.loadErrors().join('\n') : problem);
                return;
            }
            Order order;
            if (existing) {
                if (revision) {
                    const auto eligible = ledger.checkPaidEditWindow(prior.orderId, QDateTime::currentDateTime());
                    if (!eligible.ok) { QMessageBox::warning(parent, "Edit order", eligible.error); return; }
                    if (!requestPaidEdit(parent, dataDir, prior.orderId, prior.activeBillId)) return;
                }
                if (!loadOrder(dataDir, prior.orderId, order, problem)) {
                    QMessageBox::warning(parent, "Open order", problem); return;
                }
                if (!revision) {
                    std::vector<PriceChange> changes;
                    if ((prior.status == store::Status::OnHold && !order.resume(QDateTime::currentDateTime(), &problem)) ||
                        !order.reprice([&](const QString& id, Product& p) { return catalog.findById(id, p); },
                            changes, &problem)) {
                        QMessageBox::warning(parent, "Resume order", problem); return;
                    }
                    QStringList summary;
                    for (const auto& change : changes)
                        summary << QStringLiteral("%1: price %2 to %3, discount %4 to %5")
                        .arg(change.productId, money(change.oldPricePaisa), money(change.newPricePaisa),
                            money(change.oldDiscountPaisa), money(change.newDiscountPaisa));
                    if (!summary.isEmpty()) QMessageBox::information(parent, "Updated prices", summary.join('\n'));
                }
            }
            QHash<QString, qint64> available;
            order.setAvailability([&available](const Product& p) { return available.value(p.id); });
            const auto reloadAvailability = [&](QWidget* window) {
                QLockFile lock(QDir(dataDir).filePath(".checkout.lock"));
                QString error;
                if (!lock.tryLock(0)) { QMessageBox::information(window, "Stock", "Inventory is busy. Try again."); return false; }
                if (!ledger.load() || !catalog.load(QDir(dataDir).filePath("Product.txt"), &error)) {
                    QMessageBox::warning(window, "Stock", error.isEmpty() ? ledger.loadErrors().join('\n') : error); return false;
                }
                const auto reserved = ledger.reservedByProduct(QDateTime::currentDateTime(), prior.orderId);
                available.clear();
                for (const auto& p : catalog.products()) available[p.id] = qMax<qint64>(0, p.stockMilli - reserved.value(p.id));
                if (revision) {
                    for (const auto& line : prior.lines) {
                        auto& amount = available[line.productId];
                        amount += qMin(line.qtyMilli, std::numeric_limits<qint64>::max() - amount);
                    }
                }
                return true;
                };
            QVector<SeasonDiscount> seasons;
            if (!loadSeasons(dataDir, seasons, problem)) {
                QMessageBox::warning(parent, "Seasonal discounts", problem); return;
            }
            DiscountRates rates;
            if (!revision)
                rates.specialBasisPoints = prior.approvedSpecialBasisPoints;
            rates.seasonalBasisPoints = highestSeasonRate(seasons, QDate::currentDate());
            QDialog dialog(parent);
            const QString title = revision ? "Revise paid order " + prior.orderId :
                existing ? "Continue order " + prior.orderId : QStringLiteral("New Order");
            dialog.setWindowTitle(title);
            dialog.setObjectName("orderWorkspace");
            dialog.resize(1120, 650);
            auto* layout = new QVBoxLayout(&dialog);
            layout->setContentsMargins(24, 20, 24, 20);
            layout->setSpacing(14);
            auto* heading = new QLabel(title, &dialog);
            heading->setObjectName(QStringLiteral("pageTitle"));
            layout->addWidget(heading);

            auto* identity = new QHBoxLayout;
            auto* customer = new QLineEdit(prior.customerName, &dialog);
            customer->setPlaceholderText("Customer name");
            auto* contact = new QLineEdit(prior.contact, &dialog);
            contact->setPlaceholderText("Contact (optional)");
            const auto specialLabel = [](int rate) {
                return QStringLiteral("Special discount (%1%)").arg(
                    BillCalculator::formatPercentBasisPoints(rate));
                };
            auto* special = new QPushButton(specialLabel(rates.specialBasisPoints), &dialog);
            special->setAutoDefault(false);
            identity->addWidget(customer); identity->addWidget(contact); identity->addWidget(special);
            layout->addLayout(identity);
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
            table->setSelectionMode(QAbstractItemView::NoSelection);
            table->setShowGrid(false);
            auto* cartWorkspace = new QHBoxLayout;
            cartWorkspace->setSpacing(16);
            auto* cart = new QVBoxLayout;
            cart->addWidget(cartHeading);
            cart->addWidget(table, 1);
            cartWorkspace->addLayout(cart, 1);

            auto* checkoutPanel = new QFrame(&dialog);
            checkoutPanel->setProperty("panel", true);
            checkoutPanel->setObjectName("orderSummary");
            checkoutPanel->setMinimumWidth(270);
            auto* checkout = new QVBoxLayout(checkoutPanel);
            checkout->setSpacing(10);
            auto* summaryTitle = new QLabel("Order Summary", checkoutPanel);
            summaryTitle->setProperty("heading", true);
            checkout->addWidget(summaryTitle);
            checkout->setContentsMargins(14, 12, 14, 12);
            auto* close = new QPushButton(QStringLiteral("Close"), checkoutPanel);
            auto* hold = new QPushButton(QStringLiteral("Hold Order"), checkoutPanel);
            auto* generate = new QPushButton(QStringLiteral("Generate Bill"), checkoutPanel);
            generate->setProperty("role", "primary");
            for (auto* button : { close, hold, generate }) button->setAutoDefault(false);
            auto* totals = new QGridLayout;
            totals->setHorizontalSpacing(16);
            auto* subtotal = new QLabel(checkoutPanel);
            auto* tax = new QLabel(checkoutPanel);
            auto* productDiscount = new QLabel(checkoutPanel);
            auto* seasonalDiscount = new QLabel(checkoutPanel);
            auto* specialDiscount = new QLabel(checkoutPanel);
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
            addTotal(1, QStringLiteral("Product discounts"), productDiscount);
            addTotal(2, QStringLiteral("Seasonal discount"), seasonalDiscount);
            addTotal(3, QStringLiteral("Special discount"), specialDiscount);
            addTotal(4, QStringLiteral("Tax"), tax);
            checkout->addLayout(totals);
            auto* payablePanel = new QFrame(checkoutPanel);
            payablePanel->setObjectName("payablePanel");
            auto* payableLayout = new QVBoxLayout(payablePanel);
            auto* payableLabel = new QLabel("Estimated payable", payablePanel);
            payableLabel->setProperty("muted", true);
            payableLayout->addWidget(payableLabel);
            payableLayout->addWidget(payable);
            checkout->addWidget(payablePanel);
            checkout->addWidget(generate);
            checkout->addWidget(hold);
            checkout->addStretch();
            checkout->addWidget(close);
            cartWorkspace->addWidget(checkoutPanel);
            layout->addLayout(cartWorkspace, 1);

            const auto refreshTotals = [&] {
                Bill estimate;
                const bool hasEstimate = !order.items().isEmpty()
                    && BillCalculator::calculate(order, rates, estimate);
                const auto displayAmount = [&](qint64 amount) {
                    return hasEstimate || order.items().isEmpty() ? money(amount) : QStringLiteral("\u2014");
                    };
                for (int row = 0; row < table->rowCount(); ++row) {
                    table->item(row, 3)->setText(hasEstimate
                        ? money(estimate.lines.at(row).amounts.payablePaisa) : QStringLiteral("\u2014"));
                }
                subtotal->setText(displayAmount(estimate.totals.subtotalPaisa));
                tax->setText(displayAmount(estimate.totals.taxPaisa));
                productDiscount->setText(displayAmount(estimate.totals.discountPaisa));
                seasonalDiscount->setText(displayAmount(estimate.seasonalDiscountPaisa));
                specialDiscount->setText(displayAmount(estimate.specialDiscountPaisa));
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
                        if (!reloadAvailability(&dialog)) return;
                        if (!changeQuantity(&dialog, line.product.unit, [&](AtLimitChoice choice) {
                            return order.setQuantity(line.product.id, amount, &error, choice);
                            }, error)) return;
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
                generate->setEnabled((!startOrderButton || startOrderButton->isEnabled()) && !order.items().isEmpty());
                hold->setVisible(!revision);
                hold->setEnabled((!startOrderButton || startOrderButton->isEnabled()) && !order.items().isEmpty());
                };
            QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
            const auto addProduct = [&](QWidget* promptParent, const QString& digits, QString* name = nullptr) {
                Product product;
                if (!lookupProduct(promptParent, dataDir, product, digits)) {
                    return false;
                }
                if (revision) {
                    for (const auto& line : prior.lines) {
                        if (line.productId == product.id) {
                            product.pricePaisa = line.unitPricePaisa;
                            product.discountPaisa = line.discountPerUnitPaisa;
                            product.taxPaisa = line.taxPerUnitPaisa;
                            product.acronym = line.acronym;
                            product.barcode = line.barcode;
                            break;
                        }
                    }
                }
                const qint64 quantity = product.unit == Unit::Count
                    ? 1000 : requestQuantity(promptParent, product, 1000);
                if (quantity == 0) {
                    return false;
                }

                QString error;
                if (!reloadAvailability(promptParent)) return false;
                if (!changeQuantity(promptParent, product.unit, [&](AtLimitChoice choice) {
                    return order.add(product, quantity, &error, choice);
                    }, error)) return false;
                if (name) *name = product.name;
                refresh();
                return true;
                };
            const auto scanCart = [&] {
                scanOrderProducts(&dialog, [&](QWidget* scanner, const QString& digits) {
                    QString name;
                    return addProduct(scanner, digits, &name) ? name : QString();
                }, [&] {
                    QList<ScanCartLine> lines;
                    for (const auto& item : order.items())
                        lines.append(ScanCartLine{item.product.name, quantityText(item.product, item.quantityMilli)});
                    return lines;
                });
                barcode->setFocus();
            };
            QObject::connect(scan, &QPushButton::clicked, &dialog, scanCart);
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
                if (!CheckoutStorage::saveCart(dataDir, order, customer->text().trimmed(),
                    contact->text().trimmed(), prior, true,
                    rates.specialBasisPoints, error)) {
                    QMessageBox::warning(&dialog, "Hold order", error); return;
                }
                dialog.accept();
                });
            QObject::connect(special, &QPushButton::clicked, &dialog, [&] {
                if (!authorizeManager(&dialog, "Approve special discount")) return;
                bool ok = false;
                const double requested = QInputDialog::getDouble(&dialog, "Special discount", "Percent (0 to 8)",
                    rates.specialBasisPoints / 100.0, -9999, 9999, 2, &ok);
                if (!ok) return;
                rates.specialBasisPoints = BillCalculator::clampSpecialBasisPoints(qRound(requested * 100));
                special->setText(specialLabel(rates.specialBasisPoints));
                refreshTotals();
                });
            QObject::connect(generate, &QPushButton::clicked, &dialog, [&] {
                if (customer->text().trimmed().isEmpty()) {
                    customer->setFocus();
                    QMessageBox::information(&dialog, "Checkout", "Enter the customer name."); return;
                }
                Bill bill;
                QString error;
                const QDate billDate = QDate::currentDate();
                if (!loadSeasons(dataDir, seasons, error)) {
                    QMessageBox::warning(&dialog, "Seasonal discounts", error); return;
                }
                rates.seasonalBasisPoints = highestSeasonRate(seasons, billDate);
                refreshTotals();
                if (!BillCalculator::calculate(order, rates, bill, &error)) {
                    QMessageBox::warning(&dialog, "Generate Bill", error); return;
                }
                PaymentInput payment;
                if (!shop::collectPayment(&dialog, order, bill,
                    revision ? prior.paymentPaisa : -1, payment)) return;
                const auto now = QDateTime::currentDateTime();
                if (billDate != now.date()) {
                    QMessageBox::information(&dialog, "Checkout", "The date changed. Review today's discounts."); return;
                }
                CheckoutDetails details;
                details.orderId = prior.orderId;
                details.expectedBillId = revision ? prior.activeBillId : QString();
                details.previous = prior;
                details.customerName = customer->text().trimmed();
                details.customerContact = contact->text().trimmed();
                details.paymentMode = payment.mode.toLower();
                details.merchantName = payment.provider;
                details.paymentPaisa = payment.receivedPaisa;
                generate->setEnabled(false);
                CheckoutReceipt receipt;
                if (!CheckoutStorage::save(dataDir, bill, details, now,
                    receipt, &error)) {
                    if (receipt.recoveryRequired) {
                        if (startOrderButton) startOrderButton->setEnabled(false);
                        dialog.reject();
                        error += QStringLiteral("\nCheckout is locked for this session. Check the saved files before restarting.");
                    }
                    QMessageBox::warning(&dialog, QStringLiteral("Save Bill"), error);
                    generate->setEnabled(!receipt.recoveryRequired);
                    return;
                }
                if (revision) finishPaidEdit(dataDir, prior.orderId);
                bill.orderId = receipt.orderId;
                bill.billId = receipt.billId;
                bill.customerName = details.customerName;
                bill.billDate = now.date();
                bill.billTime = now.time();
                bill.paymentStatus = details.status;
                bill.paymentMode = details.paymentMode;
                bill.paymentProvider = payment.provider;
                bill.amountReceivedPaisa = payment.receivedPaisa;
                bill.balanceReturnedPaisa = payment.changePaisa;
                dialog.accept();
                showBill(&dialog, bill, details, receipt);
                });

            refresh();
            barcode->setFocus();
            if (!existing) QTimer::singleShot(0, &dialog, scanCart);
            dialog.exec();
        }

    } // namespace

    void openProductLookup(QWidget* parent, const QString& dataDir)
    {
        QDialog dialog(parent);
        dialog.setWindowTitle("Product Lookup");
        dialog.resize(490, 270);
        auto* layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(20, 18, 20, 16);
        layout->setSpacing(12);

        auto* heading = new QLabel("Product Lookup", &dialog);
        heading->setObjectName("pageTitle");
        layout->addWidget(heading);

        auto* scan = new QPushButton("Scan with camera", &dialog);
        scan->setProperty("role", "primary");
        scan->setAutoDefault(false);
        scan->setMinimumHeight(44);
        layout->addWidget(scan);

        auto* manual = new QFrame(&dialog);
        manual->setProperty("panel", true);
        auto* manualLayout = new QVBoxLayout(manual);
        manualLayout->setContentsMargins(12, 10, 12, 12);
        auto* digits = new QLineEdit(manual);
        digits->setPlaceholderText("e.g. 123456");
        digits->setMaxLength(6);
        digits->setInputMethodHints(Qt::ImhDigitsOnly);
        digits->setValidator(new QRegularExpressionValidator(QRegularExpression("[1-9][0-9]{5}"), digits));
        auto* label = new QLabel("&Enter six-digit barcode", manual);
        label->setBuddy(digits);
        manualLayout->addWidget(label);
        auto* inputRow = new QHBoxLayout;
        auto* find = new QPushButton("Find Product", manual);
        find->setEnabled(false);
        inputRow->addWidget(digits, 1);
        inputRow->addWidget(find);
        manualLayout->addLayout(inputRow);
        layout->addWidget(manual);
        layout->addStretch();

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        layout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        const auto lookup = [&](const QString& code) {
            if (code.isEmpty()) return;
            Product p;
            if (lookupProduct(&dialog, dataDir, p, code)) showProduct(&dialog, p, dataDir);
            };
        QObject::connect(scan, &QPushButton::clicked, &dialog, [&] { lookup(scanProductBarcode(&dialog)); });
        QObject::connect(digits, &QLineEdit::textChanged, &dialog, [&] { find->setEnabled(digits->hasAcceptableInput()); });
        QObject::connect(find, &QPushButton::clicked, &dialog, [&] { lookup(digits->text()); });
        find->setAutoDefault(false);
        QObject::connect(digits, &QLineEdit::returnPressed, find, &QPushButton::click);
        digits->setFocus();
        dialog.exec();
    }

    void openOrderDialog(QWidget* parent, const QString& dataDir)
    {
        recordOrder(parent, dataDir, nullptr);
    }

    MainWindow::MainWindow(QWidget* parent)
        : QMainWindow(parent)
    {
        setWindowTitle(QStringLiteral("Super Duper Ultra Mega Store"));
        resize(1020, 520);

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
        role->setObjectName("cashierRole");
        header->addWidget(title, 1);
        header->addWidget(role);
        layout->addLayout(header);

        auto* cashierTitle = new QLabel("Cashier workspace", content);
        cashierTitle->setProperty("heading", true);
        layout->addWidget(cashierTitle);
        auto* workArea = new QHBoxLayout;
        workArea->setSpacing(14);
        auto* cashier = new QGridLayout;
        cashier->setSpacing(10);
        const auto action = [content, cashier](const QString& label, int row, int column) {
            auto* button = new QPushButton(label, content);
            button->setProperty("role", "tile");
            button->setMinimumHeight(68);
            button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
            cashier->addWidget(button, row, column);
            return button;
            };
        auto* orderButton = action(QStringLiteral("New Order"), 0, 0);
        orderButton->setObjectName(QStringLiteral("newOrder"));
        auto* resumeButton = action(QStringLiteral("Resume Held Order"), 0, 1);
        auto* findOrder = action(QStringLiteral("Find Order"), 1, 0);
        auto* printBill = new QPushButton(QStringLiteral("Reprint Bill"), content);
        auto* lookupButton = action(QStringLiteral("Product Lookup"), 1, 1);
        lookupButton->setObjectName(QStringLiteral("productLookup"));
        cashier->setColumnStretch(0, 1);
        cashier->setColumnStretch(1, 1);
        workArea->addLayout(cashier, 3);

        auto* managerPanel = new QFrame(content);
        managerPanel->setProperty("panel", true);
        managerPanel->setObjectName("managerAccessCard");
        auto* managerLayout = new QVBoxLayout(managerPanel);
        managerLayout->setContentsMargins(16, 14, 16, 14);
        auto* managerTitle = new QLabel(QStringLiteral("Manager Access"), managerPanel);
        managerTitle->setProperty("heading", true);
        auto* managerDetails = new QLabel(QStringLiteral("PIN required"), managerPanel);
        managerDetails->setProperty("muted", true);
        auto* managerButton = new QPushButton(QStringLiteral("Open Manager Panel"), managerPanel);
        managerLayout->addWidget(managerTitle);
        managerLayout->addWidget(managerDetails);
        auto* managerActions = new QLabel("Inventory, products, discounts and reports", managerPanel);
        managerActions->setWordWrap(true);
        managerActions->setProperty("muted", true);
        managerLayout->addWidget(managerActions);
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
        utilityHeader->addWidget(printBill);
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
            if (!dataDir.isEmpty()) openProductLookup(this, dataDir);
            });
        connect(orderButton, &QPushButton::clicked, this, [this, orderButton, dataDirectory] {
            const QString dataDir = dataDirectory();
            if (!dataDir.isEmpty()) {
                recordOrder(this, dataDir, orderButton);
            }
            });
        const auto browse = [this, orderButton, dataDirectory](bool holdsOnly) {
            const QString dir = dataDirectory();
            if (dir.isEmpty()) return;
            QString error;
            store::CheckoutStorage ledger(dir, nullptr);
            if (!refreshOrderStates(dir, QDateTime::currentDateTime(), error) || !ledger.load()) {
                QMessageBox::warning(this, "Orders", error.isEmpty() ? ledger.loadErrors().join('\n') : error); return;
            }
            QList<store::OrderRecord> records;
            for (const auto& id : ledger.orderIds()) {
                store::OrderRecord row;
                if (!ledger.order(id, &row)) continue;
                const bool held = row.status == store::Status::OnHold;
                if (holdsOnly && !held) continue;
                records.append(row);
            }
            if (records.isEmpty()) { QMessageBox::information(this, "Orders", holdsOnly ? "No held orders." : "No saved orders."); return; }
            QDialog picker(this);
            const QString title = holdsOnly ? "Resume Held Order" : "Find Order";
            picker.setWindowTitle(title);
            picker.resize(710, 420);
            auto* box = new QVBoxLayout(&picker);
            box->setContentsMargins(18, 16, 18, 16);
            box->setSpacing(10);
            auto* heading = new QLabel(title, &picker);
            heading->setObjectName("pageTitle");
            box->addWidget(heading);

            auto* filter = new QLineEdit(&picker);
            filter->setPlaceholderText("Search by order ID or customer");
            filter->setAccessibleName("Search orders by ID or customer");
            filter->setClearButtonEnabled(true);
            box->addWidget(filter);

            auto* table = makeTable(&picker, { "Order ID", "Customer", "Status",
                holdsOnly ? QStringLiteral("Hold expires") : QStringLiteral("Order date") });
            table->horizontalHeader()->setStretchLastSection(false);
            table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
            table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
            table->verticalHeader()->setDefaultSectionSize(38);
            table->setSelectionMode(QAbstractItemView::SingleSelection);
            table->setAlternatingRowColors(true);
            table->setShowGrid(false);
            for (const auto& row : records) {
                addTableRow(table, { row.orderId, row.customerName.isEmpty() ? QStringLiteral("\u2014") : row.customerName,
                    row.status, holdsOnly ? row.holdExpiry.toString("dd MMM, HH:mm:ss")
                                          : row.orderDate.toString(Qt::ISODate) });
            }
            box->addWidget(table, 1);
            auto* empty = new QLabel("No orders match your search.", &picker);
            empty->setProperty("muted", true);
            empty->hide();
            box->addWidget(empty);

            auto* actions = new QHBoxLayout;
            auto* cancel = new QPushButton(holdsOnly ? "Cancel Hold" : "Cancel Order", &picker);
            auto* viewBill = new QPushButton("View Bill", &picker);
            auto* close = new QPushButton("Close", &picker);
            auto* open = new QPushButton(holdsOnly ? "Resume Order" : "Open Order", &picker);
            open->setProperty("role", "primary");
            for (auto* button : { cancel, viewBill, close, open }) button->setAutoDefault(false);
            actions->addWidget(cancel);
            if (!holdsOnly) actions->addWidget(viewBill);
            else viewBill->hide();
            actions->addStretch();
            actions->addWidget(close);
            actions->addWidget(open);
            box->addLayout(actions);

            const auto selected = [&]() -> const store::OrderRecord* {
                const int index = table->currentRow();
                return index >= 0 && !table->isRowHidden(index) ? &records.at(index) : nullptr;
                };
            const auto select = [&] {
                const auto* row = selected();
                viewBill->setEnabled(row && !row->activeBillId.isEmpty());
                cancel->setEnabled(row && (row->status == store::Status::OnHold || row->status == store::Status::InProgress));
                open->setEnabled(row != nullptr);
                };
            connect(table, &QTableWidget::currentCellChanged, &picker, select);
            connect(filter, &QLineEdit::textChanged, &picker, [&](const QString& query) {
                int first = -1;
                const auto text = query.trimmed();
                for (int index = 0; index < table->rowCount(); ++index) {
                    const auto& row = records.at(index);
                    const bool visible = row.orderId.contains(text, Qt::CaseInsensitive) ||
                        row.customerName.contains(text, Qt::CaseInsensitive);
                    table->setRowHidden(index, !visible);
                    if (visible && first < 0) first = index;
                }
                if (!selected()) table->setCurrentCell(first, first < 0 ? -1 : 0);
                empty->setVisible(first < 0);
                select();
                });
            connect(viewBill, &QPushButton::clicked, &picker, [&] {
                const auto* row = selected();
                if (!row || row->activeBillId.isEmpty()) return;
                Bill bill;
                if (!loadActiveBill(dir, row->orderId, bill, error)) {
                    QMessageBox::warning(&picker, "Bill", error); return;
                }
                CheckoutDetails details;
                details.customerName = row->customerName;
                details.paymentMode = row->paymentMode;
                details.status = row->status;
                showBill(&picker, bill, details, { row->orderId, row->activeBillId, false });
                });
            const auto openSelected = [&] { if (selected()) picker.accept(); };
            connect(open, &QPushButton::clicked, &picker, openSelected);
            connect(table, &QTableWidget::cellDoubleClicked, &picker, openSelected);
            connect(filter, &QLineEdit::returnPressed, &picker, openSelected);
            connect(close, &QPushButton::clicked, &picker, &QDialog::reject);
            connect(cancel, &QPushButton::clicked, &picker, [&] {
                const auto* row = selected();
                if (!row) return;
                if (QMessageBox::question(&picker, "Cancel order", "Cancel order " + row->orderId + "?") != QMessageBox::Yes) return;
                if (!CheckoutStorage::cancelCart(dir, *row, error))
                    QMessageBox::warning(&picker, "Cancel order", error);
                else picker.reject();
                });
            table->selectRow(0);
            select();
            filter->setFocus();
            if (picker.exec() != QDialog::Accepted || !selected()) return;
            const auto row = *selected();
            if (row.status == store::Status::Archived) {
                Bill bill;
                if (!loadActiveBill(dir, row.orderId, bill, error)) { QMessageBox::warning(this, "Bill", error); return; }
                CheckoutDetails details;
                details.customerName = row.customerName; details.status = row.status; details.paymentMode = row.paymentMode;
                showBill(this, bill, details, { row.orderId,row.activeBillId,false }); return;
            }
            if (row.status != store::Status::Paid && row.status != store::Status::OnHold && row.status != store::Status::InProgress) {
                QMessageBox::information(this, "Order", QStringLiteral("Order %1 is %2.").arg(row.orderId, row.status)); return;
            }
            if (orderButton->isEnabled()) recordOrder(this, dir, orderButton, row);
            };
        connect(findOrder, &QPushButton::clicked, this, [browse] { browse(false); });
        connect(resumeButton, &QPushButton::clicked, this, [browse] { browse(true); });
        connect(printBill, &QPushButton::clicked, this, [this, dataDirectory] {
            const auto dir = dataDirectory(); if (!dir.isEmpty()) reprintBill(this, dir);
            });
        const auto sweep = [this] {
            const auto dir = findDataDirectory();
            QString error;
            if (!dir.isEmpty() && !refreshOrderStates(dir, QDateTime::currentDateTime(), error))
                statusBar()->showMessage(error, 5000);
            };
        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, sweep);
        timer->start(15000);
        sweep();
        connect(scanButton, &QPushButton::clicked, this, [this, decodedOutput] {
            const QString digits = scanProductBarcode(this);
            if (!digits.isEmpty()) {
                decodedOutput->setText(digits);
            }
            });
    }

} // namespace shop
