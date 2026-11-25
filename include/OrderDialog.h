#pragma once

#include "Order.h"
#include "Product.h"
#include <QDialog>
#include <QString>
#include <QVector>
#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QTimer;

namespace shop {

    class OrderDialog : public QDialog {
        Q_OBJECT

    public:
        using BarcodeLookupFn = std::function<bool(const QString&, Product&)>;

        explicit OrderDialog(QWidget* parent = nullptr);

        void setBarcodeLookup(BarcodeLookupFn lookup);
        void setProductLookup(Order::ProductLookupFn lookup);
        const Order& currentOrder() const;
        const QVector<Order>& heldOrders() const;

    public slots:
        bool addBarcode(const QString& barcode);
        void resetCart();

    signals:
        void checkoutRequested(const shop::Order& order, const QString& customer);
        void orderHeld(const shop::Order& order);
        void heldOrderReleased(const shop::Order& order);

    private slots:
        void onAddClicked();
        void onItemChanged(QTableWidgetItem* item);
        void onRemoveClicked();
        void onClearClicked();
        void onHoldClicked();
        void onCheckoutClicked();
        void onResumeClicked();
        void onCancelHeldClicked();
        void sweepExpired();

    private:
        enum Column { ColName = 0, ColPrice, ColDiscount, ColQty, ColTotal, ColCount };

        void buildUi();
        Order makeOrder();
        qint64 reservedFor(const Product& product) const;
        AtLimitChoice askAtLimit(const Product& product);
        void refreshCart();
        void refreshHeld();

        BarcodeLookupFn barcodeLookup_;
        Order::ProductLookupFn productLookup_;
        Order order_;
        QVector<Order> held_;
        bool updating_ = false;

        QLineEdit* customerEdit_ = nullptr;
        QLineEdit* barcodeEdit_ = nullptr;
        QPushButton* addButton_ = nullptr;
        QTableWidget* table_ = nullptr;
        QLabel* totalLabel_ = nullptr;
        QPushButton* removeButton_ = nullptr;
        QPushButton* clearButton_ = nullptr;
        QPushButton* holdButton_ = nullptr;
        QPushButton* checkoutButton_ = nullptr;
        QListWidget* heldList_ = nullptr;
        QPushButton* resumeButton_ = nullptr;
        QPushButton* cancelHeldButton_ = nullptr;
        QTimer* sweepTimer_ = nullptr;
    };

    void openOrderDialog(QWidget* parent, const QString& dataDir);

}