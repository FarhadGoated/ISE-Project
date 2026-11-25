#include "OrderDialog.h"

#include "ProductCatalog.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace shop {

    namespace {

        QString formatMoney(qint64 paisa) {
            const bool negative = paisa < 0;
            const qint64 value = negative ? -paisa : paisa;
            const QString text = QString("%1.%2")
                .arg(value / 100)
                .arg(value % 100, 2, 10, QChar('0'));
            return negative ? "-" + text : text;
        }

        QString formatQty(qint64 quantityMilli, bool weighted) {
            if (!weighted) {
                return QString::number(quantityMilli / 1000);
            }
            return QString("%1.%2")
                .arg(quantityMilli / 1000)
                .arg(quantityMilli % 1000, 3, 10, QChar('0'));
        }

        bool parseQty(const QString& text, bool weighted, qint64& quantityMilli) {
            bool ok = false;
            const QString clean = text.trimmed();
            if (!weighted) {
                const qint64 pieces = clean.toLongLong(&ok);
                if (!ok) {
                    return false;
                }
                quantityMilli = pieces * 1000;
                return true;
            }
            const double kg = clean.toDouble(&ok);
            if (!ok) {
                return false;
            }
            quantityMilli = static_cast<qint64>(std::llround(kg * 1000.0));
            return true;
        }

        qint64 lineTotal(const OrderItem& item) {
            const qint64 net = item.product.pricePaisa - item.product.discountPaisa;
            return (net * item.quantityMilli + 500) / 1000;
        }

        bool isWeighted(const Product& product) {
            return product.unit != Unit::Count;
        }

    }

    OrderDialog::OrderDialog(QWidget* parent)
        : QDialog(parent), order_() {
        setWindowTitle("Cashier Cart");
        resize(900, 650);
        order_ = makeOrder();
        buildUi();
        refreshCart();
        refreshHeld();
        sweepTimer_ = new QTimer(this);
        connect(sweepTimer_, &QTimer::timeout, this, &OrderDialog::sweepExpired);
        sweepTimer_->start(15000);
    }

    Order OrderDialog::makeOrder() {
        return Order([this](const Product& product) {
            qint64 stock = product.stockMilli;
            Product latest;
            if (productLookup_ && productLookup_(product.id, latest)) {
                stock = latest.stockMilli;
            }
            return stock - reservedFor(product);
            });
    }

    qint64 OrderDialog::reservedFor(const Product& product) const {
        const QDateTime now = QDateTime::currentDateTime();
        qint64 reserved = 0;
        for (const Order& held : held_) {
            if (held.hasExpired(now)) {
                continue;
            }
            for (const OrderItem& item : held.items()) {
                if (item.product.id == product.id) {
                    reserved += item.quantityMilli;
                }
            }
        }
        return reserved;
    }

    void OrderDialog::buildUi() {
        QVBoxLayout* mainLayout = new QVBoxLayout(this);

        QHBoxLayout* topRow = new QHBoxLayout();
        topRow->addWidget(new QLabel("Customer:"));
        customerEdit_ = new QLineEdit();
        customerEdit_->setPlaceholderText("Customer name");
        topRow->addWidget(customerEdit_, 1);
        topRow->addWidget(new QLabel("Barcode:"));
        barcodeEdit_ = new QLineEdit();
        barcodeEdit_->setPlaceholderText("Scan or type barcode");
        topRow->addWidget(barcodeEdit_, 1);
        addButton_ = new QPushButton("Add");
        topRow->addWidget(addButton_);
        mainLayout->addLayout(topRow);

        table_ = new QTableWidget(0, ColCount);
        table_->setHorizontalHeaderLabels(
            { "Product", "Unit Price", "Discount", "Quantity", "Line Total" });
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::DoubleClicked |
            QAbstractItemView::EditKeyPressed |
            QAbstractItemView::AnyKeyPressed);
        mainLayout->addWidget(table_, 3);

        QHBoxLayout* actionRow = new QHBoxLayout();
        removeButton_ = new QPushButton("Remove Selected");
        clearButton_ = new QPushButton("Clear Cart");
        holdButton_ = new QPushButton("Hold");
        checkoutButton_ = new QPushButton("Checkout");
        totalLabel_ = new QLabel();
        actionRow->addWidget(removeButton_);
        actionRow->addWidget(clearButton_);
        actionRow->addStretch();
        actionRow->addWidget(totalLabel_);
        actionRow->addWidget(holdButton_);
        actionRow->addWidget(checkoutButton_);
        mainLayout->addLayout(actionRow);

        mainLayout->addWidget(new QLabel("Held Orders"));
        heldList_ = new QListWidget();
        mainLayout->addWidget(heldList_, 2);

        QHBoxLayout* heldRow = new QHBoxLayout();
        resumeButton_ = new QPushButton("Resume Selected");
        cancelHeldButton_ = new QPushButton("Cancel Held Order");
        heldRow->addStretch();
        heldRow->addWidget(resumeButton_);
        heldRow->addWidget(cancelHeldButton_);
        mainLayout->addLayout(heldRow);

        connect(addButton_, &QPushButton::clicked, this, &OrderDialog::onAddClicked);
        connect(barcodeEdit_, &QLineEdit::returnPressed, this, &OrderDialog::onAddClicked);
        connect(table_, &QTableWidget::itemChanged, this, &OrderDialog::onItemChanged);
        connect(removeButton_, &QPushButton::clicked, this, &OrderDialog::onRemoveClicked);
        connect(clearButton_, &QPushButton::clicked, this, &OrderDialog::onClearClicked);
        connect(holdButton_, &QPushButton::clicked, this, &OrderDialog::onHoldClicked);
        connect(checkoutButton_, &QPushButton::clicked, this, &OrderDialog::onCheckoutClicked);
        connect(resumeButton_, &QPushButton::clicked, this, &OrderDialog::onResumeClicked);
        connect(cancelHeldButton_, &QPushButton::clicked, this, &OrderDialog::onCancelHeldClicked);
    }

    void OrderDialog::setBarcodeLookup(BarcodeLookupFn lookup) {
        barcodeLookup_ = std::move(lookup);
    }

    void OrderDialog::setProductLookup(Order::ProductLookupFn lookup) {
        productLookup_ = std::move(lookup);
    }

    const Order& OrderDialog::currentOrder() const {
        return order_;
    }

    const QVector<Order>& OrderDialog::heldOrders() const {
        return held_;
    }

    AtLimitChoice OrderDialog::askAtLimit(const Product& product) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle("Last stock");
        box.setText(QString("This is all the available stock of %1.").arg(product.name));
        QPushButton* sellAll = box.addButton("Sell all", QMessageBox::AcceptRole);
        QPushButton* keepOne = box.addButton("Keep one", QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == sellAll) {
            return AtLimitChoice::SellAll;
        }
        if (box.clickedButton() == keepOne) {
            return AtLimitChoice::KeepOne;
        }
        return AtLimitChoice::Ask;
    }

    bool OrderDialog::addBarcode(const QString& barcode) {
        const QString code = barcode.trimmed();
        if (code.isEmpty()) {
            return false;
        }

        Product product;
        if (!barcodeLookup_ || !barcodeLookup_(code, product)) {
            QMessageBox::warning(this, "Not found", "No product found for this barcode.");
            return false;
        }

        qint64 quantityMilli = 1000;
        if (isWeighted(product)) {
            bool ok = false;
            const double kg = QInputDialog::getDouble(
                this, "Enter weight",
                QString("Weight in kg for %1:").arg(product.name),
                0.001, 0.001, 100000.0, 3, &ok);
            if (!ok) {
                return false;
            }
            quantityMilli = static_cast<qint64>(std::llround(kg * 1000.0));
        }

        QString error;
        AddOutcome outcome = order_.add(product, quantityMilli, &error);
        if (outcome == AddOutcome::NeedsDecision) {
            const AtLimitChoice choice = askAtLimit(product);
            if (choice == AtLimitChoice::Ask) {
                return false;
            }
            outcome = order_.add(product, quantityMilli, &error, choice);
        }
        if (outcome != AddOutcome::Added) {
            if (!error.isEmpty()) {
                QMessageBox::warning(this, "Cannot add", error);
            }
            return false;
        }

        refreshCart();
        return true;
    }

    void OrderDialog::resetCart() {
        order_ = makeOrder();
        customerEdit_->clear();
        refreshCart();
    }

    void OrderDialog::onAddClicked() {
        if (addBarcode(barcodeEdit_->text())) {
            barcodeEdit_->clear();
        }
        barcodeEdit_->setFocus();
    }

    void OrderDialog::onItemChanged(QTableWidgetItem* item) {
        if (updating_ || !item || item->column() != ColQty) {
            return;
        }
        const int row = item->row();
        if (row < 0 || row >= order_.items().size()) {
            return;
        }

        const Product product = order_.items()[row].product;
        qint64 quantityMilli = 0;
        if (!parseQty(item->text(), isWeighted(product), quantityMilli)) {
            QMessageBox::warning(this, "Invalid quantity", "Please enter a valid number.");
            QTimer::singleShot(0, this, [this]() { refreshCart(); });
            return;
        }

        QString error;
        AddOutcome outcome = order_.setQuantity(product.id, quantityMilli, &error);
        if (outcome == AddOutcome::NeedsDecision) {
            const AtLimitChoice choice = askAtLimit(product);
            if (choice != AtLimitChoice::Ask) {
                outcome = order_.setQuantity(product.id, quantityMilli, &error, choice);
            }
        }
        if (outcome == AddOutcome::Rejected && !error.isEmpty()) {
            QMessageBox::warning(this, "Cannot change quantity", error);
        }
        QTimer::singleShot(0, this, [this]() { refreshCart(); });
    }

    void OrderDialog::onRemoveClicked() {
        const int row = table_->currentRow();
        if (row < 0 || row >= order_.items().size()) {
            return;
        }
        const QString id = order_.items()[row].product.id;
        order_.remove(id);
        refreshCart();
    }

    void OrderDialog::onClearClicked() {
        if (order_.items().isEmpty()) {
            return;
        }
        if (QMessageBox::question(this, "Clear cart", "Remove all items from the cart?") != QMessageBox::Yes) {
            return;
        }
        order_ = makeOrder();
        refreshCart();
    }

    void OrderDialog::onHoldClicked() {
        QString error;
        if (!order_.hold(QDateTime::currentDateTime(), &error)) {
            QMessageBox::warning(this, "Hold", error);
            return;
        }
        held_.push_back(order_);
        emit orderHeld(order_);
        order_ = makeOrder();
        customerEdit_->clear();
        refreshCart();
        refreshHeld();
    }

    void OrderDialog::onCheckoutClicked() {
        if (order_.items().isEmpty()) {
            QMessageBox::information(this, "Checkout", "The cart is empty.");
            return;
        }
        if (order_.hasExpired(QDateTime::currentDateTime())) {
            QMessageBox::warning(this, "Expired", "This held order has expired. The cart was cleared.");
            order_ = makeOrder();
            refreshCart();
            return;
        }
        emit checkoutRequested(order_, customerEdit_->text().trimmed());
    }

    void OrderDialog::onResumeClicked() {
        const int row = heldList_->currentRow();
        if (row < 0 || row >= held_.size()) {
            QMessageBox::information(this, "Resume", "Select a held order first.");
            return;
        }
        if (!order_.items().isEmpty()) {
            QMessageBox::warning(this, "Resume", "Hold or clear the current cart first.");
            return;
        }

        Order candidate = held_[row];
        QString error;
        if (!candidate.resume(QDateTime::currentDateTime(), &error)) {
            QMessageBox::warning(this, "Resume", error);
            sweepExpired();
            return;
        }

        std::vector<PriceChange> changes;
        if (productLookup_ && !candidate.reprice(productLookup_, changes, &error)) {
            QMessageBox::warning(this, "Resume", error);
            return;
        }

        held_.removeAt(row);
        order_ = candidate;
        refreshCart();
        refreshHeld();

        if (!changes.empty()) {
            QString text = "Prices changed while this order was held:\n";
            for (const PriceChange& change : changes) {
                QString name = change.productId;
                for (const OrderItem& item : order_.items()) {
                    if (item.product.id == change.productId) {
                        name = item.product.name;
                        break;
                    }
                }
                text += QString("\n%1: price %2 -> %3, discount %4 -> %5")
                    .arg(name)
                    .arg(formatMoney(change.oldPricePaisa))
                    .arg(formatMoney(change.newPricePaisa))
                    .arg(formatMoney(change.oldDiscountPaisa))
                    .arg(formatMoney(change.newDiscountPaisa));
            }
            QMessageBox::information(this, "Price changes", text);
        }
    }

    void OrderDialog::onCancelHeldClicked() {
        const int row = heldList_->currentRow();
        if (row < 0 || row >= held_.size()) {
            QMessageBox::information(this, "Cancel", "Select a held order first.");
            return;
        }
        if (QMessageBox::question(this, "Cancel held order",
            "Cancel this order and release its reserved stock?") != QMessageBox::Yes) {
            return;
        }
        const Order released = held_.takeAt(row);
        emit heldOrderReleased(released);
        refreshHeld();
    }

    void OrderDialog::sweepExpired() {
        const QDateTime now = QDateTime::currentDateTime();
        bool changed = false;
        for (int i = held_.size() - 1; i >= 0; --i) {
            if (held_[i].hasExpired(now)) {
                const Order released = held_.takeAt(i);
                emit heldOrderReleased(released);
                changed = true;
            }
        }
        if (changed) {
            refreshHeld();
        }
    }

    void OrderDialog::refreshCart() {
        updating_ = true;
        const QVector<OrderItem>& items = order_.items();
        table_->setRowCount(items.size());
        qint64 total = 0;

        for (int row = 0; row < items.size(); ++row) {
            const OrderItem& line = items[row];
            const qint64 lineSum = lineTotal(line);
            total += lineSum;

            QTableWidgetItem* name = new QTableWidgetItem(line.product.name);
            QTableWidgetItem* price = new QTableWidgetItem(formatMoney(line.product.pricePaisa));
            QTableWidgetItem* discount = new QTableWidgetItem(formatMoney(line.product.discountPaisa));
            QTableWidgetItem* qty = new QTableWidgetItem(formatQty(line.quantityMilli, isWeighted(line.product)));
            QTableWidgetItem* sum = new QTableWidgetItem(formatMoney(lineSum));

            name->setFlags(name->flags() & ~Qt::ItemIsEditable);
            price->setFlags(price->flags() & ~Qt::ItemIsEditable);
            discount->setFlags(discount->flags() & ~Qt::ItemIsEditable);
            sum->setFlags(sum->flags() & ~Qt::ItemIsEditable);

            table_->setItem(row, ColName, name);
            table_->setItem(row, ColPrice, price);
            table_->setItem(row, ColDiscount, discount);
            table_->setItem(row, ColQty, qty);
            table_->setItem(row, ColTotal, sum);
        }

        totalLabel_->setText(QString("Estimated payable (before tax): PKR %1").arg(formatMoney(total)));
        updating_ = false;
    }

    void OrderDialog::refreshHeld() {
        heldList_->clear();
        int number = 1;
        for (const Order& held : held_) {
            const QString label = held.orderId().isEmpty()
                ? QString("Held #%1").arg(number)
                : held.orderId();
            heldList_->addItem(QString("%1 | %2 items | expires %3")
                .arg(label)
                .arg(held.items().size())
                .arg(held.holdExpiry().toString("yyyy-MM-dd HH:mm:ss")));
            ++number;
        }
    }

    void openOrderDialog(QWidget* parent, const QString& dataDir) {
        ProductCatalog catalog;
        QString error;
        if (!catalog.load(QDir(dataDir).filePath("Product.txt"), &error)) {
            QMessageBox::warning(parent, "Products", error);
            return;
        }

        OrderDialog dialog(parent);
        dialog.setBarcodeLookup([&catalog](const QString& barcode, Product& product) {
            return catalog.findByBarcode(barcode, product);
            });
        dialog.setProductLookup([&catalog](const QString& id, Product& product) {
            return catalog.findById(id, product);
            });
        dialog.exec();
    }
}