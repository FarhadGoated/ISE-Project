// CheckoutStorage.h  --  Shahzeen | Storage and checkout integration
// Super Duper Ultra Mega Store, Increment 3 (C++ / Qt Widgets, uses QtCore only)
//
// Money is integer paisa, quantities are integer thousandths (count 2 pcs = 2000,
// 1.250 kg = 1250). Files follow page 2 of the team guide; lifecycle metadata lives
// in three storage-owned side files so the required text rows are never changed.
#pragma once

#include "Bill.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <functional>

namespace store {

// ---------- shared value formatting (locale independent) ----------
QString formatPkr(qint64 paisa);                         // 35345 -> "353.45"
bool    parsePkr(const QString &text, qint64 *paisa);    // "353.45" -> 35345
QString formatQty(qint64 milli, bool weighted);          // 2000,false -> "2"; 1250,true -> "1.250"
bool    parseQty(const QString &text, qint64 *milli, bool *weighted);
QString formatDate(const QDate &d);                      // YYYY-MM-DD
QString formatTime(const QTime &t);                      // HH:mm:ss
QString formatStamp(const QDateTime &dt);                // "YYYY-MM-DD HH:mm:ss" or ""
QDateTime parseStamp(const QString &text);

// ---------- records ----------
namespace Status {
inline const QString InProgress = QStringLiteral("in-progress");
inline const QString OnHold     = QStringLiteral("on-hold");
inline const QString Paid       = QStringLiteral("paid");
inline const QString Archived   = QStringLiteral("archived");
inline const QString Cancelled  = QStringLiteral("cancelled");
inline const QString Expired    = QStringLiteral("expired");
}

struct OrderLine {
    QString productId;          // stable, used for stock and reports
    QString barcode;            // barcode at the time of the sale (may change later)
    qint64  qtyMilli = 0;
    bool    weighted = false;
    // Price snapshot (stored in OrderSnapshot.txt, not in OrderDetail.txt)
    QString acronym;
    qint64  unitPricePaisa = 0;
    qint64  taxPerUnitPaisa = 0;
    qint64  discountPerUnitPaisa = 0;
};

struct OrderRecord {
    QString orderId;
    QDate   orderDate;
    QTime   orderTime;
    QString customerName;
    QString contact;            // optional
    QString status = Status::InProgress;
    qint64  paymentPaisa = 0;   // amount paid for the active bill
    QString paymentMode;        // cash / card / wallet
    QString paymentMerchant;    // provider, or "" for cash
    QList<OrderLine> lines;
    // Lifecycle metadata (Lifecycle.txt)
    QDateTime holdExpiry;       // valid while on-hold
    QDateTime paidAt;           // ORIGINAL payment timestamp, never reset by revisions
    QString activeBillId;
    int approvedSpecialBasisPoints = 0;
};

struct BillLine {
    QString acronym;
    qint64  unitPricePaisa = 0; // before tax
    qint64  qtyMilli = 0;
    bool    weighted = false;
    qint64  productDiscountPaisa = 0; // total for the line
};

struct BillRecord {
    QString billId;             // nine digits, assigned by storage when empty
    QString customerName;
    QString orderId;
    qint64  totalBeforeTax = 0;
    qint64  totalTax = 0;
    qint64  totalProductDiscount = 0;
    qint64  totalPayable = 0;
    qint64  amountReceived = 0;
    qint64  balanceReturned = 0;
    QString paymentStatus = QStringLiteral("paid"); // follows order status
    qint64  seasonalDiscount = 0;
    qint64  specialDiscount = 0;
    QDate   billDate;
    QTime   billTime;
    QList<BillLine> lines;
    // BillMeta.txt
    bool    superseded = false;
    QString supersededBy;
    QString paymentMode, paymentMerchant;
};

// Everything a report needs about one completed order (page 5, items 5-7).
struct ReportOrder {
    QString orderId;
    QString status;             // paid or archived
    QDateTime paidAt;           // original payment timestamp
    QString paymentMode;
    BillRecord activeBill;      // counted once
    QHash<QString, qint64> qtyByProduct; // Product ID -> thousandths
};

// ---------- inventory access (implemented by Anaya's ProductCatalog) ----------
class IInventory {
public:
    virtual ~IInventory() = default;
    // Current physical stock keyed by Product ID, in thousandths. Must reload from disk.
    virtual QHash<QString, qint64> physicalStock() = 0;
    // Persist a full set of changed stocks (Product ID -> new physical stock). All or nothing.
    virtual bool applyStock(const QHash<QString, qint64> &newStock, QString *error) = 0;
};

struct Result {
    bool ok = false;
    QString error;              // human readable, empty when ok
    bool recoveryFailed = false; // true: files may be inconsistent, checkout is locked
    static Result success() { Result r; r.ok = true; return r; }
    static Result failure(const QString &e) { Result r; r.error = e; return r; }
};

struct SweepResult {
    QStringList expiredOrders;
    QStringList archivedOrders;
    Result      result;
};

class CheckoutStorage {
public:
    static constexpr int HoldMinutes = 30;
    static constexpr int ArchiveHours = 168;

    // dataDir holds Order.txt, OrderDetail.txt, Bill.txt, BillDetail.txt and the side files.
    CheckoutStorage(const QString &dataDir, IInventory *inventory);

    // ---- loading ----
    // Missing files mean a fresh start. Malformed records are skipped and reported
    // with file name and line number in loadErrors(). Load errors lock writes.
    bool load();
    const QStringList &loadErrors() const { return m_loadErrors; }

    // ---- queries ----
    QStringList orderIds() const { return m_orders.keys(); }
    bool order(const QString &orderId, OrderRecord *out) const;
    QList<OrderRecord> ordersWithStatus(const QString &status) const;
    bool bill(const QString &billId, BillRecord *out) const;           // any bill, incl. superseded
    bool activeBillForOrder(const QString &orderId, BillRecord *out) const;
    QList<BillRecord> billHistory(const QString &orderId) const;       // oldest first
    QList<BillRecord> activeBills() const;                             // one per completed order

    // Reservations come only from on-hold orders whose expiry has not passed.
    QHash<QString, qint64> reservedByProduct(const QDateTime &now,
                                             const QString &excludeOrderId = QString()) const;
    // physical - reserved(excluding excludeOrderId). Never negative.
    qint64 availableStock(const QString &productId, const QDateTime &now,
                          const QString &excludeOrderId = QString());
    qint64 reservedStock(const QString &productId, const QDateTime &now) const;

    // ---- report data (Farhad) ----
    QList<ReportOrder> completedOrders() const;                        // paid + archived
    QHash<QString, qint64> soldQuantities() const;                     // active completed orders only

    // ---- lifecycle operations (each is one coordinated commit) ----
    QString nextOrderId(const QDate &date) const;
    QString nextBillId() const;
    Result holdOrder(OrderRecord order, const QDateTime &now);         // new expiry = now + 30 min
    Result saveDraft(OrderRecord order, const QDateTime& now);
    Result cancelOrder(const QString &orderId);                        // releases the reservation
    SweepResult sweep(const QDateTime &now);                           // expire holds, archive paid

    // Prepares a held order for resume. Fails (and expires it) when the deadline has passed.
    Result checkResume(const QString &orderId, const QDateTime &now, OrderRecord *out);

    // Pays a new or resumed order. bill.billId is assigned here. Order and bill rows,
    // stock and metadata are committed together.
    Result commitPaidSale(OrderRecord order, BillRecord bill, const QDateTime &now,
                          QString *assignedBillId = nullptr);

    // Replaces the active bill of a paid order. The caller already checked manager
    // approval and consumes it only when this returns ok.
    Result commitPaidRevision(OrderRecord revised, BillRecord replacement, const QDateTime &now,
                              QString *assignedBillId = nullptr);

    // Checks the 168 hour window using the ORIGINAL payment timestamp (call before opening).
    Result checkPaidEditWindow(const QString &orderId, const QDateTime &now) const;

    // ---- safety ----
    // True after a failed recovery. Checkout refuses new commits until the app restarts
    // and load() succeeds, so a sale cannot be submitted twice by accident.
    bool isLocked() const { return m_locked; }
    // Test hook: make writing the named file (e.g. "Bill.txt") fail.
    void setFaultInjection(const QString &fileName, bool alsoFailRecovery = false);

private:
    Result saveOpenOrder(OrderRecord order, const QDateTime& now, bool reserve);
    struct FileImage { QString name; QByteArray bytes; };
    Result commitAll(const QMap<QString, OrderRecord> &orders,
                     const QMap<QString, BillRecord> &bills,
                     const QHash<QString, qint64> &stockChanges);
    QList<FileImage> buildImages(const QMap<QString, OrderRecord> &orders,
                                 const QMap<QString, BillRecord> &bills) const;
    Result checkLines(const OrderRecord &o, const QDateTime &now,
                      const QHash<QString, qint64> &oldQty, const QHash<QString, qint64> &stock) const;
    bool writeFile(const QString &path, const QByteArray &bytes, QString *error) const;
    QString path(const QString &name) const;

    QString m_dir;
    IInventory *m_inv;
    QMap<QString, OrderRecord> m_orders;
    QMap<QString, BillRecord> m_bills;
    QStringList m_loadErrors;
    bool m_locked = false;
    QString m_faultFile;
    bool m_faultRecovery = false;
};

} // namespace store

// Entry point used by the current cashier window.

namespace shop {
struct CheckoutDetails {
    QString customerName, customerContact, paymentMode, merchantName;
    QString status = QStringLiteral("paid");
    qint64 paymentPaisa = 0;
    QString orderId;             // Empty for a new sale
    QString expectedBillId;      // Active bill approved for this revision
    store::OrderRecord previous; // Snapshot opened for editing
};

struct CheckoutReceipt {
    QString orderId, billId;
    bool recoveryRequired = false;
};

class CheckoutStorage {
public:
    static bool saveCart(const QString& dataDir, const Order& cart, const QString& customer,
                         const QString& contact, const store::OrderRecord& previous,
                         bool reserve, int approvedSpecialBasisPoints, QString& error);
    static bool cancelCart(const QString& dataDir, const store::OrderRecord& previous, QString& error);
    static bool save(const QString& dataDir, const Bill& bill,
                     const CheckoutDetails& details, const QDateTime& now,
                     CheckoutReceipt& receipt, QString* error = nullptr);
};
}
