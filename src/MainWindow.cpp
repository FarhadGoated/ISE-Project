#include "MainWindow.h"

#include "BarcodeFileIO.h"
#include "BarcodeGenerator.h"
#include "BarcodeReader.h"
#include "BarcodeTypes.h"
#include "BillCalculator.h"
#include "CameraCapture.h"
#include "CheckoutStorage.h"
#include "ImageProcessor.h"
#include "InputValidator.h"
#include "ProductCatalog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWidget>

#include <cmath>
#include <vector>

namespace shop {

    namespace {

        QString scanBarcode(QWidget* parent)
        {
            QDialog dialog(parent);
            dialog.setWindowTitle(QStringLiteral("Scan barcode"));

            auto* layout = new QVBoxLayout(&dialog);
            auto* instructions = new QLabel(
                QStringLiteral("Center the barcode horizontally with both white margins visible."),
                &dialog);
            instructions->setWordWrap(true);

            auto* preview = new QVideoWidget(&dialog);
            preview->setMinimumSize(480, 270);

            auto* cancelButton = new QPushButton(QStringLiteral("Cancel"), &dialog);
            layout->addWidget(instructions);
            layout->addWidget(preview, 1);
            layout->addWidget(cancelButton);

            QString decoded;
            QString cameraError;
            bool scanning = true;
            CameraCapture capture;

            QObject::connect(cancelButton, &QPushButton::clicked,
                &dialog, &QDialog::reject);

            QObject::connect(&dialog, &QDialog::finished, &capture, [&](int) {
                scanning = false;
                });

            QObject::connect(&capture, &CameraCapture::cameraError, &dialog,
                [&](const QString& message) {
                    if (!scanning) {
                        return;
                    }

                    cameraError = message;
                    dialog.reject();
                });

            QObject::connect(&capture, &CameraCapture::frameReady, &dialog,
                [&](const QImage& frame) {
                    if (!scanning || frame.isNull()) {
                        return;
                    }

                    const int insetX = frame.width() / 20;
                    const int insetY = frame.height() / 4;
                    const QRect scanArea = frame.rect().adjusted(
                        insetX, insetY, -insetX, -insetY);

                    for (const auto& runs : ImageProcessor::extractCameraRuns(frame, scanArea)) {
                        if (BarcodeReader::decodeCode39Digits(runs, decoded)) {
                            dialog.accept();
                            return;
                        }
                    }
                });

            QTimer::singleShot(0, &dialog, [&] {
                if (scanning) {
                    capture.start(preview);
                }
                });

            const int result = dialog.exec();
            scanning = false;
            capture.stop();

            if (!cameraError.isEmpty()) {
                QMessageBox::warning(parent, QStringLiteral("Camera"), cameraError);
            }

            return result == QDialog::Accepted ? decoded : QString{};
        }

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
            auto* form = new QFormLayout(&dialog);

            const auto addField = [&](const QString& label, const QString& value) {
                auto* field = new QLabel(&dialog);
                field->setTextFormat(Qt::PlainText);
                field->setText(value);
                field->setWordWrap(true);
                form->addRow(label, field);
                };

            addField(QStringLiteral("Product ID"), product.id);
            addField(QStringLiteral("Barcode"), product.barcode);
            addField(QStringLiteral("Acronym"), product.acronym);
            addField(QStringLiteral("Name"), product.name);
            addField(QStringLiteral("Measurement unit"),
                product.unit == Unit::Count ? QStringLiteral("Count") : QStringLiteral("Weight (kg)"));
            addField(QStringLiteral("Price per unit"), money(product.pricePaisa));
            addField(QStringLiteral("Tax per unit"), money(product.taxPaisa));
            addField(QStringLiteral("Discount per unit"), money(product.discountPaisa));
            addField(QStringLiteral("Available quantity"), quantityText(product, product.stockMilli));
            addField(QStringLiteral("Barcode path"), product.barcodePath);

            const QPixmap barcode(QDir(dataDir).absoluteFilePath(product.barcodePath));
            if (!barcode.isNull()) {
                auto* image = new QLabel(&dialog);
                image->setPixmap(barcode.scaled(441, 140, Qt::KeepAspectRatio));
                form->addRow(image);
            }

            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
            form->addRow(buttons);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            dialog.exec();
        }

        bool lookupProduct(QWidget* parent, const QString& dataDir, Product& product)
        {
            ProductCatalog catalog;
            QString error;
            const QString path = QDir(dataDir).filePath(QStringLiteral("Product.txt"));
            if (!catalog.load(path, &error)) {
                QMessageBox::warning(parent, QStringLiteral("Product Catalog"),
                    error.isEmpty() ? QStringLiteral("Could not load %1.").arg(path) : error);
                return false;
            }

            const QString barcode = scanBarcode(parent);
            if (barcode.isEmpty()) {
                return false;
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

        void recordOrder(QWidget* parent, const QString& dataDir, QPushButton* startOrderButton)
        {
            Order order;
            QDialog dialog(parent);
            dialog.setWindowTitle(QStringLiteral("New Order"));
            dialog.resize(760, 430);
            auto* layout = new QVBoxLayout(&dialog);
            auto* controls = new QHBoxLayout;
            auto* scan = new QPushButton(QStringLiteral("Scan Product"), &dialog);
            auto* description = new QPushButton(QStringLiteral("Description"), &dialog);
            auto* edit = new QPushButton(QStringLiteral("Edit Quantity"), &dialog);
            auto* remove = new QPushButton(QStringLiteral("Remove Item"), &dialog);
            controls->addWidget(scan);
            controls->addWidget(description);
            controls->addWidget(edit);
            controls->addWidget(remove);
            layout->addLayout(controls);

            auto* table = makeTable(&dialog, { QStringLiteral("Product"), QStringLiteral("Barcode"),
                QStringLiteral("Quantity"), QStringLiteral("Unit price") });
            layout->addWidget(table);
            auto* actions = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
            auto* generate = actions->addButton(QStringLiteral("Generate Bill"), QDialogButtonBox::ActionRole);
            layout->addWidget(actions);

            const auto updateButtons = [&] {
                const bool selected = table->currentRow() >= 0;
                description->setEnabled(selected);
                edit->setEnabled(selected);
                remove->setEnabled(selected);
                };
            const auto refresh = [&] {
                table->setRowCount(0);
                for (const OrderItem& item : order.items()) {
                    addTableRow(table, { item.product.name, item.product.barcode,
                        quantityText(item.product, item.quantityMilli), money(item.product.pricePaisa) });
                }
                if (table->rowCount() > 0) {
                    table->selectRow(0);
                }
                generate->setEnabled(startOrderButton->isEnabled() && !order.items().isEmpty());
                updateButtons();
                };
            const auto selectedItem = [&]() -> const OrderItem* {
                const int row = table->currentRow();
                return row >= 0 && row < order.items().size() ? &order.items()[row] : nullptr;
                };
            QObject::connect(table, &QTableWidget::currentCellChanged, &dialog,
                [&](int, int, int, int) { updateButtons(); });
            QObject::connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            QObject::connect(scan, &QPushButton::clicked, &dialog, [&] {
                Product product;
                if (!lookupProduct(&dialog, dataDir, product)) {
                    return;
                }
                const qint64 quantity = requestQuantity(&dialog, product, 1000);
                if (quantity == 0) {
                    return;
                }

                QString error;
                if (!order.add(product, quantity, &error)) {
                    QMessageBox::warning(&dialog, QStringLiteral("Add Product"), error);
                    return;
                }
                refresh();
                });
            QObject::connect(description, &QPushButton::clicked, &dialog, [&] {
                if (const auto* item = selectedItem()) {
                    showProduct(&dialog, item->product, dataDir);
                }
                });
            QObject::connect(edit, &QPushButton::clicked, &dialog, [&] {
                const auto* selected = selectedItem();
                if (!selected) {
                    return;
                }
                const OrderItem item = *selected;
                const qint64 quantity = requestQuantity(&dialog, item.product, item.quantityMilli);
                if (quantity == 0) {
                    return;
                }
                QString error;
                if (!order.setQuantity(item.product.id, quantity, &error)) {
                    QMessageBox::warning(&dialog, QStringLiteral("Edit Quantity"), error);
                    return;
                }
                refresh();
                });
            QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
                if (const auto* item = selectedItem()) {
                    const QString productId = item->product.id;
                    order.remove(productId);
                    refresh();
                }
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
            dialog.exec();
        }

    } // namespace

    MainWindow::MainWindow(QWidget* parent)
        : QMainWindow(parent)
    {
        setWindowTitle(QStringLiteral("RandomKidz Point of Sale"));
        resize(850, 520);

        auto* content = new QWidget(this);
        setCentralWidget(content);
        auto* layout = new QVBoxLayout(content);
        layout->setContentsMargins(24, 24, 24, 24);
        layout->setSpacing(16);
        content->setObjectName(QStringLiteral("posHome"));
        content->setStyleSheet(QStringLiteral(R"(
            QWidget#posHome { background: #121316; color: #eeeff2; }
            QWidget#posHome QFrame#barcodePanel {
                background: #1a1c20; border: 1px solid #303238; border-radius: 12px;
            }
            QWidget#posHome QLabel { color: #bfc3cb; }
            QWidget#posHome QLabel#posTitle { font-size: 28px; font-weight: 700; color: #f5f5f7; }
            QWidget#posHome QLabel#toolsTitle { font-size: 17px; font-weight: 600; color: #eeeff2; }
            QWidget#posHome QLabel#barcodePreview {
                background: #f4f4f6; color: #494d57; border-radius: 8px;
            }
            QWidget#posHome QLineEdit {
                background: #121316; color: #eeeff2; border: 1px solid #42454e;
                border-radius: 6px; padding: 7px 9px;
            }
            QWidget#posHome QPushButton {
                background: transparent; color: #d9dbe2; border: 1px solid transparent;
                border-radius: 6px; padding: 6px 9px; min-height: 21px; font-weight: 500;
            }
            QWidget#posHome QPushButton:hover { background: #2b2e36; }
            QWidget#posHome QPushButton:pressed { background: #343842; }
            QWidget#posHome QPushButton:focus, QWidget#posHome QLineEdit:focus {
                border-color: #a8b1ff;
            }
            QWidget#posHome QPushButton#newOrder:enabled,
            QWidget#posHome QPushButton#productLookup:enabled {
                padding: 8px 20px; border-radius: 8px; font-weight: 600;
            }
            QWidget#posHome QPushButton#newOrder:enabled,
            QWidget#posHome QPushButton#productLookup:enabled {
                background: #1a1c20; border-color: #4a4d57; color: #eeeff2;
            }
            QWidget#posHome QPushButton#newOrder:hover:enabled,
            QWidget#posHome QPushButton#productLookup:hover:enabled {
                background: #2b2e36; border-color: #a8b1ff;
            }
            QWidget#posHome QPushButton#newOrder:pressed:enabled,
            QWidget#posHome QPushButton#productLookup:pressed:enabled {
                background: #343842; border-color: #a8b1ff;
            }
            QWidget#posHome QPushButton#newOrder:focus:enabled,
            QWidget#posHome QPushButton#productLookup:focus:enabled {
                border-color: #a8b1ff;
            }
            QWidget#posHome QPushButton:disabled { color: #777b86; background: transparent; }
        )"));

        auto* inputLabel = new QLabel(QStringLiteral("Six-digit number"), content);
        auto* input = new QLineEdit(content);
        inputLabel->setBuddy(input);
        input->setFocus();

        auto* generateButton = new QPushButton(QStringLiteral("Generate"), content);
        auto* saveButton = new QPushButton(QStringLiteral("Save PNG"), content);
        auto* readButton = new QPushButton(QStringLiteral("Read PNG"), content);
        auto* scanButton = new QPushButton(QStringLiteral("Scan Camera"), content);
        auto* lookupButton = new QPushButton(QStringLiteral("Product Lookup"), content);
        auto* orderButton = new QPushButton(QStringLiteral("New Order"), content);
        orderButton->setObjectName(QStringLiteral("newOrder"));
        lookupButton->setObjectName(QStringLiteral("productLookup"));
        orderButton->setMinimumHeight(46);
        lookupButton->setMinimumHeight(46);
        for (auto* button : { orderButton, lookupButton, generateButton, saveButton, readButton, scanButton }) {
            button->setCursor(Qt::PointingHandCursor);
        }
        saveButton->setEnabled(false);

        auto* preview = new QLabel(QStringLiteral("Barcode preview"), content);
        preview->setAlignment(Qt::AlignCenter);
        preview->setObjectName(QStringLiteral("barcodePreview"));
        preview->setMinimumSize(270, 160);

        auto* decodedLabel = new QLabel(QStringLiteral("Decoded number"), content);
        auto* decodedOutput = new QLineEdit(content);
        decodedOutput->setReadOnly(true);
        decodedLabel->setBuddy(decodedOutput);

        auto* title = new QLabel(QStringLiteral("RandomKidz Point of Sale"), content);
        title->setObjectName(QStringLiteral("posTitle"));
        layout->addWidget(title);
        auto* posActions = new QHBoxLayout;
        posActions->setSpacing(10);
        posActions->addWidget(orderButton);
        posActions->addWidget(lookupButton);
        posActions->addStretch();
        layout->addLayout(posActions);

        auto* panel = new QFrame(content);
        panel->setObjectName(QStringLiteral("barcodePanel"));
        auto* panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(18, 16, 18, 18);
        auto* toolsTitle = new QLabel(QStringLiteral("Barcode Tools"), panel);
        toolsTitle->setObjectName(QStringLiteral("toolsTitle"));
        panelLayout->addWidget(toolsTitle);

        auto* toolsRow = new QHBoxLayout;
        toolsRow->setSpacing(18);
        auto* fields = new QVBoxLayout;
        fields->addWidget(inputLabel);
        fields->addWidget(input);
        auto* generateActions = new QHBoxLayout;
        generateActions->addWidget(generateButton);
        generateActions->addWidget(saveButton);
        fields->addLayout(generateActions);
        fields->addSpacing(10);
        auto* readActions = new QHBoxLayout;
        readActions->addWidget(readButton);
        readActions->addWidget(scanButton);
        fields->addLayout(readActions);
        fields->addWidget(decodedLabel);
        fields->addWidget(decodedOutput);
        fields->addStretch();
        toolsRow->addLayout(fields, 1);
        toolsRow->addWidget(preview, 2);
        panelLayout->addLayout(toolsRow, 1);
        layout->addWidget(panel, 1);

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
        connect(scanButton, &QPushButton::clicked, this, [this, decodedOutput] {
            const QString digits = scanBarcode(this);
            if (!digits.isEmpty()) {
                decodedOutput->setText(digits);
            }
            });
    }

} // namespace shop
