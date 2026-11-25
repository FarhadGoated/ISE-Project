// CheckoutStorage.cpp  --  Shahzeen | Storage and checkout integration
#include "CheckoutStorage.h"
#include "ProductCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QLockFile>
#include <QSet>
#include <QStringConverter>
#include <QTextStream>
#include <QVector>

#include <limits>

namespace store {

// =====================================================================
//  Locale independent formatting and parsing
// =====================================================================

QString formatPkr(qint64 paisa)
{
    const bool neg = paisa < 0;
    return QStringLiteral("%1%2.%3")
        .arg(neg ? QStringLiteral("-") : QString())
        .arg(qAbs(paisa / 100))
        .arg(qAbs(paisa % 100), 2, 10, QLatin1Char('0'));
}

bool parsePkr(const QString &text, qint64 *paisa)
{
    static const QRegularExpression re(QStringLiteral("^(-?)(\\d+)(?:\\.(\\d{1,2}))?$"));
    const auto m = re.match(text);
    if (!m.hasMatch() || !paisa)
        return false;
    QString frac = m.captured(3);
    while (frac.size() < 2)
        frac += QLatin1Char('0');
    bool ok = false;
    const qint64 value = (m.captured(1) + m.captured(2) + frac).toLongLong(&ok);
    if (ok)
        *paisa = value;
    return ok;
}

QString formatQty(qint64 milli, bool weighted)
{
    if (!weighted)
        return QString::number(milli / 1000);
    return QStringLiteral("%1.%2").arg(milli / 1000).arg(milli % 1000, 3, 10, QLatin1Char('0'));
}

bool parseQty(const QString &text, qint64 *milli, bool *weighted)
{
    static const QRegularExpression re(QStringLiteral("^(\\d+)(?:\\.(\\d{1,3}))?$"));
    const auto m = re.match(text);
    if (!m.hasMatch() || !milli || !weighted)
        return false;
    const bool w = m.captured(2).size() > 0;
    QString frac = m.captured(2);
    while (frac.size() < 3)
        frac += QLatin1Char('0');
    bool ok = false;
    const qint64 value = (m.captured(1) + frac).toLongLong(&ok);
    if (!ok)
        return false;
    *milli = value;
    *weighted = w;
    return true;
}

QString formatDate(const QDate &d)
{
    if (!d.isValid())
        return QString();
    return QStringLiteral("%1-%2-%3")
        .arg(d.year(), 4, 10, QLatin1Char('0'))
        .arg(d.month(), 2, 10, QLatin1Char('0'))
        .arg(d.day(), 2, 10, QLatin1Char('0'));
}

QString formatTime(const QTime &t)
{
    if (!t.isValid())
        return QString();
    return QStringLiteral("%1:%2:%3")
        .arg(t.hour(), 2, 10, QLatin1Char('0'))
        .arg(t.minute(), 2, 10, QLatin1Char('0'))
        .arg(t.second(), 2, 10, QLatin1Char('0'));
}

QString formatStamp(const QDateTime &dt)
{
    if (!dt.isValid())
        return QString();
    return formatDate(dt.date()) + QLatin1Char(' ') + formatTime(dt.time());
}

QDateTime parseStamp(const QString &text)
{
    if (text.isEmpty())
        return QDateTime();
    return QDateTime::fromString(text, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

// =====================================================================
//  Record tokenizer / writer (space separated, quoted text, \\ and \" escapes)
// =====================================================================

namespace {

struct Tok { QString text; bool quoted; };

bool tokenize(const QString &line, QList<Tok> *out, QString *err)
{
    int i = 0;
    const int n = line.size();
    while (i < n) {
        const QChar c = line[i];
        if (c.isSpace()) { ++i; continue; }
        if (c == QLatin1Char('"')) {
            ++i;
            QString s;
            bool closed = false;
            while (i < n) {
                const QChar d = line[i];
                if (d == QLatin1Char('\\') && i + 1 < n) { s += line[i + 1]; i += 2; continue; }
                if (d == QLatin1Char('"')) { closed = true; ++i; break; }
                s += d; ++i;
            }
            if (!closed) { *err = QStringLiteral("unterminated quoted text"); return false; }
            out->append({s, true});
        } else {
            int j = i;
            while (j < n && !line[j].isSpace()) ++j;
            out->append({line.mid(i, j - i), false});
            i = j;
        }
    }
    return true;
}

QString quote(const QString &s)
{
    bool need = s.isEmpty();
    for (const QChar c : s)
        if (c.isSpace() || c == QLatin1Char('"') || c == QLatin1Char('\\') || c == QLatin1Char('#'))
            need = true;
    if (!need)
        return s;
    QString r = QStringLiteral("\"");
    for (const QChar c : s) {
        if (c == QLatin1Char('"') || c == QLatin1Char('\\'))
            r += QLatin1Char('\\');
        r += c;
    }
    r += QLatin1Char('"');
    return r;
}

class Row {
public:
    Row &text(const QString &s) { m_parts << quote(s); return *this; }
    Row &raw(const QString &s) { m_parts << s; return *this; }
    Row &money(qint64 p) { m_parts << formatPkr(p); return *this; }
    QByteArray finish(bool hash) const
    {
        QString s = m_parts.join(QLatin1Char(' '));
        if (hash)
            s += QStringLiteral(" #");
        s += QLatin1Char('\n');
        return s.toUtf8();
    }
private:
    QStringList m_parts;
};

// Reads a table. fn returns an empty string when the row is accepted, otherwise the reason.
using RowFn = std::function<QString(const QStringList &)>;

void readTable(const QString &path, const QString &name, int fields, bool hasHash,
               QStringList *errors, const RowFn &fn)
{
    QFile f(path);
    if (!f.exists())
        return; // fresh start
    if (!f.open(QIODevice::ReadOnly)) {
        errors->append(QStringLiteral("%1: cannot open (%2)").arg(name, f.errorString()));
        return;
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    int lineNo = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        ++lineNo;
        if (line.trimmed().isEmpty())
            continue;
        auto fail = [&](const QString &why) {
            errors->append(QStringLiteral("%1 line %2: %3").arg(name).arg(lineNo).arg(why));
        };
        QList<Tok> toks;
        QString err;
        if (!tokenize(line, &toks, &err)) { fail(err); continue; }
        if (hasHash) {
            if (toks.isEmpty() || toks.last().quoted || toks.last().text != QLatin1String("#")) {
                fail(QStringLiteral("record does not end with #"));
                continue;
            }
            toks.removeLast();
        }
        if (toks.size() != fields) {
            fail(QStringLiteral("expected %1 fields, found %2").arg(fields).arg(toks.size()));
            continue;
        }
        QStringList vals;
        for (const Tok &t : toks) vals << t.text;
        const QString why = fn(vals);
        if (!why.isEmpty())
            fail(why);
    }
}

QString checkBillId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^\\d{9}$"));
    return re.match(id).hasMatch() ? QString() : QStringLiteral("Bill ID '%1' is not nine digits").arg(id);
}

} // namespace

// =====================================================================
//  Construction and loading
// =====================================================================

namespace {
const char *kFiles[] = {"Order.txt", "OrderDetail.txt", "Bill.txt", "BillDetail.txt",
                        "Lifecycle.txt", "OrderSnapshot.txt", "BillMeta.txt"};
}

CheckoutStorage::CheckoutStorage(const QString &dataDir, IInventory *inventory)
    : m_dir(dataDir), m_inv(inventory)
{
    QDir().mkpath(m_dir);
}

QString CheckoutStorage::path(const QString &name) const
{
    return QDir(m_dir).filePath(name);
}

bool CheckoutStorage::load()
{
    m_orders.clear();
    m_bills.clear();
    m_loadErrors.clear();
    QStringList &errs = m_loadErrors;

    // Orders
    readTable(path("Order.txt"), "Order.txt", 9, true, &errs, [&](const QStringList &v) -> QString {
        OrderRecord o;
        o.orderId = v[0];
        o.orderDate = QDate::fromString(v[1], QStringLiteral("yyyy-MM-dd"));
        o.orderTime = QTime::fromString(v[2], QStringLiteral("HH:mm:ss"));
        if (!o.orderDate.isValid() || !o.orderTime.isValid())
            return QStringLiteral("bad order date or time");
        o.customerName = v[3];
        o.contact = v[4];
        o.status = v[5];
        if (!parsePkr(v[6], &o.paymentPaisa))
            return QStringLiteral("bad payment amount '%1'").arg(v[6]);
        o.paymentMode = v[7];
        o.paymentMerchant = v[8];
        if (m_orders.contains(o.orderId))
            return QStringLiteral("duplicate Order ID %1").arg(o.orderId);
        m_orders.insert(o.orderId, o);
        return {};
    });

    readTable(path("OrderDetail.txt"), "OrderDetail.txt", 4, false, &errs, [&](const QStringList &v) -> QString {
        auto it = m_orders.find(v[0]);
        if (it == m_orders.end())
            return QStringLiteral("unknown Order ID %1").arg(v[0]);
        OrderLine l;
        l.productId = v[1];
        l.barcode = v[2];
        if (!parseQty(v[3], &l.qtyMilli, &l.weighted))
            return QStringLiteral("bad quantity '%1'").arg(v[3]);
        it->lines.append(l);
        return {};
    });

    readTable(path("OrderSnapshot.txt"), "OrderSnapshot.txt", 6, true, &errs, [&](const QStringList &v) -> QString {
        auto it = m_orders.find(v[0]);
        if (it == m_orders.end())
            return QStringLiteral("unknown Order ID %1").arg(v[0]);
        for (OrderLine &l : it->lines) {
            if (l.productId == v[1]) {
                l.acronym = v[2];
                if (!parsePkr(v[3], &l.unitPricePaisa) || !parsePkr(v[4], &l.taxPerUnitPaisa)
                    || !parsePkr(v[5], &l.discountPerUnitPaisa))
                    return QStringLiteral("bad snapshot amount");
                return {};
            }
        }
        return QStringLiteral("snapshot for product %1 has no order line").arg(v[1]);
    });

    readTable(path("Lifecycle.txt"), "Lifecycle.txt", 4, true, &errs, [&](const QStringList &v) -> QString {
        auto it = m_orders.find(v[0]);
        if (it == m_orders.end())
            return QStringLiteral("unknown Order ID %1").arg(v[0]);
        it->holdExpiry = parseStamp(v[1]);
        it->paidAt = parseStamp(v[2]);
        if ((!v[1].isEmpty() && !it->holdExpiry.isValid()) || (!v[2].isEmpty() && !it->paidAt.isValid()))
            return QStringLiteral("bad timestamp");
        it->activeBillId = v[3];
        return {};
    });

    // Bills
    readTable(path("Bill.txt"), "Bill.txt", 14, true, &errs, [&](const QStringList &v) -> QString {
        BillRecord b;
        const QString idErr = checkBillId(v[0]);
        if (!idErr.isEmpty())
            return idErr;
        b.billId = v[0];
        b.customerName = v[1];
        b.orderId = v[2];
        if (!parsePkr(v[3], &b.totalBeforeTax) || !parsePkr(v[4], &b.totalTax)
            || !parsePkr(v[5], &b.totalProductDiscount) || !parsePkr(v[6], &b.totalPayable)
            || !parsePkr(v[7], &b.amountReceived) || !parsePkr(v[8], &b.balanceReturned)
            || !parsePkr(v[10], &b.seasonalDiscount) || !parsePkr(v[11], &b.specialDiscount))
            return QStringLiteral("bad money amount");
        b.paymentStatus = v[9];
        b.billDate = QDate::fromString(v[12], QStringLiteral("yyyy-MM-dd"));
        b.billTime = QTime::fromString(v[13], QStringLiteral("HH:mm:ss"));
        if (!b.billDate.isValid() || !b.billTime.isValid())
            return QStringLiteral("bad bill date or time");
        if (m_bills.contains(b.billId))
            return QStringLiteral("duplicate Bill ID %1").arg(b.billId);
        m_bills.insert(b.billId, b);
        return {};
    });

    readTable(path("BillDetail.txt"), "BillDetail.txt", 5, true, &errs, [&](const QStringList &v) -> QString {
        auto it = m_bills.find(v[0]);
        if (it == m_bills.end())
            return QStringLiteral("unknown Bill ID %1").arg(v[0]);
        BillLine l;
        l.acronym = v[1];
        if (!parsePkr(v[2], &l.unitPricePaisa) || !parseQty(v[3], &l.qtyMilli, &l.weighted)
            || !parsePkr(v[4], &l.productDiscountPaisa))
            return QStringLiteral("bad bill line value");
        it->lines.append(l);
        return {};
    });

    readTable(path("BillMeta.txt"), "BillMeta.txt", 3, true, &errs, [&](const QStringList &v) -> QString {
        auto it = m_bills.find(v[0]);
        if (it == m_bills.end())
            return QStringLiteral("unknown Bill ID %1").arg(v[0]);
        it->superseded = (v[1] == QLatin1String("true"));
        it->supersededBy = v[2];
        return {};
    });

    // Cross checks
    for (auto it = m_orders.begin(); it != m_orders.end(); ++it) {
        OrderRecord &o = it.value();
        const bool completed = (o.status == Status::Paid || o.status == Status::Archived);
        if (completed) {
            const auto active = m_bills.constFind(o.activeBillId);
            if (active == m_bills.constEnd() || active->orderId != o.orderId || active->superseded) {
                // Fall back to the newest bill of this order that is not superseded.
                QString pick;
                for (const BillRecord &b : m_bills)
                    if (b.orderId == o.orderId && !b.superseded && b.billId > pick)
                        pick = b.billId;
                if (pick.isEmpty())
                    errs << QStringLiteral("Order %1 is %2 but has no active bill").arg(o.orderId, o.status);
                else
                    o.activeBillId = pick;
            }
            if (!o.paidAt.isValid())
                errs << QStringLiteral("Order %1 is %2 but has no original payment time").arg(o.orderId, o.status);
        }
        if (o.status == Status::OnHold && !o.holdExpiry.isValid())
            errs << QStringLiteral("Order %1 is on hold without an expiry").arg(o.orderId);
    }
    for (const BillRecord &bill : m_bills) {
        if (!m_orders.contains(bill.orderId))
            errs << QStringLiteral("Bill %1 has an unknown order ID %2").arg(bill.billId, bill.orderId);
        if (bill.lines.isEmpty())
            errs << QStringLiteral("Bill %1 has no item rows").arg(bill.billId);
    }

    // Leftovers from an interrupted or failed commit keep checkout locked.
    bool leftover = false;
    for (const char *n : kFiles) {
        for (const QString &suffix : {QStringLiteral(".bak"), QStringLiteral(".tmp")}) {
            if (QFile::exists(path(QString::fromLatin1(n) + suffix))) {
                leftover = true;
                errs << QStringLiteral("%1%2 exists: an earlier save did not finish. Restore the .bak files "
                                       "(or delete them after checking the data) before selling.")
                            .arg(QString::fromLatin1(n), suffix);
            }
        }
    }
    m_locked = leftover || !errs.isEmpty();
    return errs.isEmpty();
}

// =====================================================================
//  Queries
// =====================================================================

bool CheckoutStorage::order(const QString &orderId, OrderRecord *out) const
{
    auto it = m_orders.constFind(orderId);
    if (it == m_orders.constEnd())
        return false;
    if (out) *out = it.value();
    return true;
}

QList<OrderRecord> CheckoutStorage::ordersWithStatus(const QString &status) const
{
    QList<OrderRecord> r;
    for (const OrderRecord &o : m_orders)
        if (o.status == status)
            r << o;
    return r;
}

bool CheckoutStorage::bill(const QString &billId, BillRecord *out) const
{
    auto it = m_bills.constFind(billId);
    if (it == m_bills.constEnd())
        return false;
    if (out) *out = it.value();
    return true;
}

bool CheckoutStorage::activeBillForOrder(const QString &orderId, BillRecord *out) const
{
    auto o = m_orders.constFind(orderId);
    if (o == m_orders.constEnd() || o->activeBillId.isEmpty())
        return false;
    return bill(o->activeBillId, out);
}

QList<BillRecord> CheckoutStorage::billHistory(const QString &orderId) const
{
    QList<BillRecord> r;
    for (const BillRecord &b : m_bills)
        if (b.orderId == orderId)
            r << b;
    return r; // QMap iterates by Bill ID, which grows with time
}

QList<BillRecord> CheckoutStorage::activeBills() const
{
    QList<BillRecord> r;
    for (const OrderRecord &o : m_orders) {
        if (o.status != Status::Paid && o.status != Status::Archived)
            continue;
        BillRecord b;
        if (bill(o.activeBillId, &b))
            r << b;
    }
    return r;
}

QHash<QString, qint64> CheckoutStorage::reservedByProduct(const QDateTime &now,
                                                          const QString &excludeOrderId) const
{
    QHash<QString, qint64> r;
    for (const OrderRecord &o : m_orders) {
        if (o.status != Status::OnHold || o.orderId == excludeOrderId)
            continue;
        if (!o.holdExpiry.isValid() || o.holdExpiry <= now)
            continue;
        for (const OrderLine &l : o.lines)
            r[l.productId] += l.qtyMilli;
    }
    return r;
}

qint64 CheckoutStorage::reservedStock(const QString &productId, const QDateTime &now) const
{
    return reservedByProduct(now).value(productId, 0);
}

qint64 CheckoutStorage::availableStock(const QString &productId, const QDateTime &now,
                                       const QString &excludeOrderId)
{
    const qint64 phys = m_inv->physicalStock().value(productId, 0);
    const qint64 avail = phys - reservedByProduct(now, excludeOrderId).value(productId, 0);
    return avail < 0 ? 0 : avail;
}

QList<ReportOrder> CheckoutStorage::completedOrders() const
{
    QList<ReportOrder> r;
    for (const OrderRecord &o : m_orders) {
        if (o.status != Status::Paid && o.status != Status::Archived)
            continue;
        ReportOrder ro;
        if (!bill(o.activeBillId, &ro.activeBill))
            continue; // already reported by load()
        ro.orderId = o.orderId;
        ro.status = o.status;
        ro.paidAt = o.paidAt;
        ro.paymentMode = o.paymentMode;
        for (const OrderLine &l : o.lines)
            ro.qtyByProduct[l.productId] += l.qtyMilli;
        r << ro;
    }
    return r;
}

QHash<QString, qint64> CheckoutStorage::soldQuantities() const
{
    QHash<QString, qint64> r;
    for (const ReportOrder &o : completedOrders())
        for (auto it = o.qtyByProduct.cbegin(); it != o.qtyByProduct.cend(); ++it)
            r[it.key()] += it.value();
    return r;
}

QString CheckoutStorage::nextOrderId(const QDate &date) const
{
    if (!date.isValid())
        return {};
    const QStringList ids = orderIds();
    int largest = 0;
    for (const QString &id : ids) {
        bool ok = false;
        const int sequence = id.left(6).toInt(&ok);
        if (id.size() == 8 && ok)
            largest = qMax(largest, sequence);
    }
    if (largest >= 999999)
        return {};
    return QStringLiteral("%1%2").arg(largest + 1, 6, 10, QLatin1Char('0'))
        .arg(date.day(), 2, 10, QLatin1Char('0'));
}

QString CheckoutStorage::nextBillId() const
{
    qint64 maxId = 0;
    for (auto it = m_bills.cbegin(); it != m_bills.cend(); ++it)
        maxId = qMax(maxId, it.key().toLongLong());
    if (maxId >= 999999999)
        return {};
    return QStringLiteral("%1").arg(maxId + 1, 9, 10, QLatin1Char('0'));
}

// =====================================================================
//  File images and coordinated commit
// =====================================================================

QList<CheckoutStorage::FileImage>
CheckoutStorage::buildImages(const QMap<QString, OrderRecord> &orders,
                             const QMap<QString, BillRecord> &bills) const
{
    QByteArray order, detail, snap, life, bill, bdetail, bmeta;
    for (const OrderRecord &o : orders) {
        order += Row().text(o.orderId).raw(formatDate(o.orderDate)).raw(formatTime(o.orderTime))
                     .text(o.customerName).text(o.contact).text(o.status).money(o.paymentPaisa)
                     .text(o.paymentMode).text(o.paymentMerchant).finish(true);
        for (const OrderLine &l : o.lines) {
            detail += Row().text(o.orderId).text(l.productId).text(l.barcode)
                          .raw(formatQty(l.qtyMilli, l.weighted)).finish(false);
            snap += Row().text(o.orderId).text(l.productId).text(l.acronym).money(l.unitPricePaisa)
                        .money(l.taxPerUnitPaisa).money(l.discountPerUnitPaisa).finish(true);
        }
        life += Row().text(o.orderId).text(formatStamp(o.holdExpiry)).text(formatStamp(o.paidAt))
                    .text(o.activeBillId).finish(true);
    }
    for (const BillRecord &b : bills) {
        bill += Row().raw(b.billId).text(b.customerName).text(b.orderId).money(b.totalBeforeTax)
                    .money(b.totalTax).money(b.totalProductDiscount).money(b.totalPayable)
                    .money(b.amountReceived).money(b.balanceReturned).text(b.paymentStatus)
                    .money(b.seasonalDiscount).money(b.specialDiscount).raw(formatDate(b.billDate))
                    .raw(formatTime(b.billTime)).finish(true);
        for (const BillLine &l : b.lines)
            bdetail += Row().raw(b.billId).text(l.acronym).money(l.unitPricePaisa)
                           .raw(formatQty(l.qtyMilli, l.weighted)).money(l.productDiscountPaisa).finish(true);
        bmeta += Row().raw(b.billId).raw(b.superseded ? "true" : "false").text(b.supersededBy).finish(true);
    }
    return {{"Order.txt", order},       {"OrderDetail.txt", detail}, {"OrderSnapshot.txt", snap},
            {"Lifecycle.txt", life},    {"Bill.txt", bill},          {"BillDetail.txt", bdetail},
            {"BillMeta.txt", bmeta}};
}

bool CheckoutStorage::writeFile(const QString &p, const QByteArray &bytes, QString *error) const
{
    if (!m_faultFile.isEmpty() && !m_faultRecovery
        && QFileInfo(p).fileName().startsWith(m_faultFile)) {
        *error = QStringLiteral("cannot write %1 (simulated fault)").arg(QFileInfo(p).fileName());
        return false;
    }
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("cannot write %1: %2").arg(QFileInfo(p).fileName(), f.errorString());
        return false;
    }
    if (f.write(bytes) != bytes.size() || !f.flush()) {
        *error = QStringLiteral("short write to %1: %2").arg(QFileInfo(p).fileName(), f.errorString());
        return false;
    }
    f.close();
    return true;
}

Result CheckoutStorage::commitAll(const QMap<QString, OrderRecord> &orders,
                                  const QMap<QString, BillRecord> &bills,
                                  const QHash<QString, qint64> &stockChanges)
{
    if (m_locked) {
        Result r = Result::failure(QStringLiteral("Checkout is locked after a failed save. Do not retry the sale; "
                                                  "restart the app and check the data files first."));
        r.recoveryFailed = true;
        return r;
    }

    const QList<FileImage> images = buildImages(orders, bills);
    auto cleanup = [&](bool bak) {
        for (const FileImage &im : images) {
            QFile::remove(path(im.name + ".tmp"));
            if (bak) QFile::remove(path(im.name + ".bak"));
        }
    };

    // 1. Stage every new file next to the old one. A failure here changes nothing.
    for (const FileImage &im : images) {
        QString err;
        if (!writeFile(path(im.name + ".tmp"), im.bytes, &err)) {
            cleanup(false);
            return Result::failure(err + QStringLiteral(". Nothing was saved; the cart is unchanged, you can retry."));
        }
    }

    // 2. Back up the current files.
    QSet<QString> existed;
    for (const FileImage &im : images) {
        const QString cur = path(im.name), bak = path(im.name + ".bak");
        QFile::remove(bak);
        if (QFile::exists(cur)) {
            if (!QFile::copy(cur, bak)) {
                cleanup(true);
                return Result::failure(QStringLiteral("cannot back up %1. Nothing was saved; you can retry.").arg(im.name));
            }
            existed.insert(im.name);
        }
    }

    // 3. Apply stock first (it has its own all-or-nothing write), remember the old values.
    QHash<QString, qint64> oldStock;
    if (!stockChanges.isEmpty()) {
        const QHash<QString, qint64> cur = m_inv->physicalStock();
        for (auto it = stockChanges.cbegin(); it != stockChanges.cend(); ++it)
            oldStock[it.key()] = cur.value(it.key(), 0);
        QString err;
        if (!m_inv->applyStock(stockChanges, &err)) {
            cleanup(true);
            return Result::failure(QStringLiteral("stock was not updated (%1). Nothing was saved; you can retry.").arg(err));
        }
    }

    // 4. Swap files in. On any failure, roll everything back.
    QStringList swapped;
    QString swapError;
    for (const FileImage &im : images) {
        const QString cur = path(im.name), tmp = path(im.name + ".tmp");
        bool ok = true;
        if (!m_faultFile.isEmpty() && m_faultRecovery && im.name.startsWith(m_faultFile))
            ok = false; // simulated dead disk
        if (ok && QFile::exists(cur))
            ok = QFile::remove(cur);
        if (ok)
            ok = QFile::rename(tmp, cur);
        if (!ok) {
            swapError = QStringLiteral("cannot replace %1").arg(im.name);
            break;
        }
        swapped << im.name;
    }

    if (swapError.isEmpty()) {
        cleanup(true);
        m_orders = orders;
        m_bills = bills;
        return Result::success();
    }

    // ---- recovery ----
    bool recovered = true;
    QStringList toRestore = swapped;
    // The file that failed may be half replaced (removed but not renamed): restore it too.
    for (const FileImage &im : images)
        if (!swapped.contains(im.name) && !QFile::exists(path(im.name)) && existed.contains(im.name))
            toRestore << im.name;
    for (const QString &name : toRestore) {
        const QString cur = path(name), bak = path(name + ".bak");
        if (m_faultRecovery && !m_faultFile.isEmpty()) { recovered = false; continue; }
        if (QFile::exists(cur) && !QFile::remove(cur)) {
            recovered = false;
            continue;
        }
        if (existed.contains(name)) {
            if (!QFile::copy(bak, cur))
                recovered = false;
        }
    }
    if (!oldStock.isEmpty()) {
        QString err;
        if (m_faultRecovery || !m_inv->applyStock(oldStock, &err))
            recovered = false;
    }

    if (recovered) {
        cleanup(true);
        return Result::failure(swapError + QStringLiteral(". All earlier changes were undone; you can retry."));
    }
    // Keep .bak files for manual repair and lock checkout so the sale cannot be sent twice.
    for (const FileImage &im : images)
        QFile::remove(path(im.name + ".tmp"));
    m_locked = true;
    Result r = Result::failure(swapError + QStringLiteral(" and the automatic undo also failed. "
                               "Do NOT submit this sale again. Backups (*.bak) were kept in %1.").arg(m_dir));
    r.recoveryFailed = true;
    return r;
}

void CheckoutStorage::setFaultInjection(const QString &fileName, bool alsoFailRecovery)
{
    m_faultFile = fileName;
    m_faultRecovery = alsoFailRecovery;
}

// =====================================================================
//  Validation helpers
// =====================================================================

Result CheckoutStorage::checkLines(const OrderRecord &o, const QDateTime &now,
                                   const QHash<QString, qint64> &oldQty,
                                   const QHash<QString, qint64> &stock) const
{
    if (o.lines.isEmpty())
        return Result::failure(QStringLiteral("Order %1 has no items.").arg(o.orderId));
    const QHash<QString, qint64> reserved = reservedByProduct(now, o.orderId);
    QHash<QString, qint64> wanted;
    for (const OrderLine &l : o.lines) {
        if (l.productId.isEmpty() || l.qtyMilli <= 0)
            return Result::failure(QStringLiteral("Order %1 has an invalid line (%2).").arg(o.orderId, l.productId));
        if (!l.weighted && l.qtyMilli % 1000 != 0)
            return Result::failure(QStringLiteral("Product %1 requires a whole quantity.").arg(l.productId));
        if (wanted.value(l.productId) > std::numeric_limits<qint64>::max() - l.qtyMilli)
            return Result::failure(QStringLiteral("Quantity is too large for product %1.").arg(l.productId));
        wanted[l.productId] += l.qtyMilli;
    }
    for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
        if (!stock.contains(it.key()))
            return Result::failure(QStringLiteral("Product %1 is not in the catalog.").arg(it.key()));
        const qint64 avail = stock.value(it.key()) - reserved.value(it.key(), 0);
        const qint64 added = it.value() - oldQty.value(it.key(), 0);
        if (added > avail)
            return Result::failure(QStringLiteral("Not enough stock for %1: available %2, needed %3.")
                                       .arg(it.key(), formatQty(qMax<qint64>(avail, 0), true),
                                            formatQty(added, true)));
    }
    return Result::success();
}

// =====================================================================
//  Lifecycle operations
// =====================================================================

Result CheckoutStorage::holdOrder(OrderRecord order, const QDateTime &now)
{
    if (m_locked)
        return commitAll({}, {}, {});
    if (!m_inv || !now.isValid() || order.orderId.isEmpty())
        return Result::failure(QStringLiteral("A hold requires an order ID, a valid time, and an inventory source."));
    auto cur = m_orders.constFind(order.orderId);
    if (cur != m_orders.constEnd() && (cur->status == Status::Paid || cur->status == Status::Archived))
        return Result::failure(QStringLiteral("Order %1 is already paid and cannot be held.").arg(order.orderId));
    const Result chk = checkLines(order, now, {}, m_inv->physicalStock());
    if (!chk.ok)
        return chk;
    order.status = Status::OnHold;
    order.holdExpiry = now.addSecs(HoldMinutes * 60); // holding again always sets a new deadline
    order.paidAt = QDateTime();
    order.activeBillId.clear();
    if (!order.orderDate.isValid()) order.orderDate = now.date();
    if (!order.orderTime.isValid()) order.orderTime = now.time();
    QMap<QString, OrderRecord> orders = m_orders;
    orders[order.orderId] = order;
    return commitAll(orders, m_bills, {});
}

Result CheckoutStorage::cancelOrder(const QString &orderId)
{
    auto it = m_orders.constFind(orderId);
    if (it == m_orders.constEnd())
        return Result::failure(QStringLiteral("Order %1 was not found.").arg(orderId));
    if (it->status != Status::OnHold && it->status != Status::InProgress)
        return Result::failure(QStringLiteral("Order %1 is %2 and cannot be cancelled.").arg(orderId, it->status));
    QMap<QString, OrderRecord> orders = m_orders;
    orders[orderId].status = Status::Cancelled;
    orders[orderId].holdExpiry = QDateTime();
    return commitAll(orders, m_bills, {}); // reservation released because status is no longer on-hold
}

Result CheckoutStorage::checkResume(const QString &orderId, const QDateTime &now, OrderRecord *out)
{
    auto it = m_orders.constFind(orderId);
    if (it == m_orders.constEnd())
        return Result::failure(QStringLiteral("Order %1 was not found.").arg(orderId));
    if (it->status != Status::OnHold)
        return Result::failure(QStringLiteral("Order %1 is %2, not on hold.").arg(orderId, it->status));
    if (it->holdExpiry <= now) {
        const SweepResult expired = sweep(now);
        if (!expired.result.ok)
            return expired.result;
        return Result::failure(QStringLiteral("The hold on order %1 has expired and its stock was released.").arg(orderId));
    }
    if (out) *out = it.value();
    return Result::success(); // the original expiry keeps running during resume
}

SweepResult CheckoutStorage::sweep(const QDateTime &now)
{
    SweepResult sr;
    QMap<QString, OrderRecord> orders = m_orders;
    QMap<QString, BillRecord> bills = m_bills;
    for (OrderRecord &o : orders) {
        if (o.status == Status::OnHold && o.holdExpiry.isValid() && o.holdExpiry <= now) {
            o.status = Status::Expired;
            sr.expiredOrders << o.orderId;
        } else if (o.status == Status::Paid && o.paidAt.isValid()
                   && o.paidAt.addSecs(qint64(ArchiveHours) * 3600) <= now) {
            o.status = Status::Archived;
            sr.archivedOrders << o.orderId;
            auto b = bills.find(o.activeBillId);
            if (b != bills.end())
                b->paymentStatus = Status::Archived; // Bill.txt status follows the order
        }
    }
    if (sr.expiredOrders.isEmpty() && sr.archivedOrders.isEmpty()) {
        sr.result = Result::success();
        return sr;
    }
    sr.result = commitAll(orders, bills, {});
    if (!sr.result.ok) {
        sr.expiredOrders.clear();
        sr.archivedOrders.clear();
    }
    return sr;
}

Result CheckoutStorage::checkPaidEditWindow(const QString &orderId, const QDateTime &now) const
{
    auto it = m_orders.constFind(orderId);
    if (it == m_orders.constEnd())
        return Result::failure(QStringLiteral("Order %1 was not found.").arg(orderId));
    if (it->status == Status::Archived)
        return Result::failure(QStringLiteral("Order %1 is archived and can only be reprinted.").arg(orderId));
    if (it->status != Status::Paid || !it->paidAt.isValid())
        return Result::failure(QStringLiteral("Order %1 is not a paid order.").arg(orderId));
    if (it->paidAt.addSecs(qint64(ArchiveHours) * 3600) <= now)
        return Result::failure(QStringLiteral("The 168 hour edit window for order %1 has passed.").arg(orderId));
    return Result::success();
}

static Result checkBillMatchesOrder(const OrderRecord &o, const BillRecord &b)
{
    if (b.orderId != o.orderId)
        return Result::failure(QStringLiteral("Bill belongs to order %1, not %2.").arg(b.orderId, o.orderId));
    if (b.lines.isEmpty())
        return Result::failure(QStringLiteral("The bill has no item rows."));
    if (b.amountReceived < b.totalPayable)
        return Result::failure(QStringLiteral("Amount received %1 does not cover payable %2.")
                                   .arg(formatPkr(b.amountReceived), formatPkr(b.totalPayable)));
    if (b.balanceReturned != b.amountReceived - b.totalPayable)
        return Result::failure(QStringLiteral("Balance returned does not equal received minus payable."));
    return Result::success();
}

Result CheckoutStorage::commitPaidSale(OrderRecord order, BillRecord bill, const QDateTime &now,
                                       QString *assignedBillId)
{
    if (m_locked)
        return commitAll({}, {}, {}); // returns the lock message
    if (!m_inv || !now.isValid() || order.orderId.isEmpty())
        return Result::failure(QStringLiteral("A sale requires an order ID, a valid time, and an inventory source."));

    auto cur = m_orders.constFind(order.orderId);
    if (cur != m_orders.constEnd()) {
        if (cur->status == Status::Paid || cur->status == Status::Archived)
            return Result::failure(QStringLiteral("Order %1 is already paid.").arg(order.orderId));
        if (cur->status == Status::OnHold && cur->holdExpiry <= now)
            return Result::failure(QStringLiteral("The hold on order %1 expired. Resume or rebuild the cart.").arg(order.orderId));
    }
    const Result billChk = checkBillMatchesOrder(order, bill);
    if (!billChk.ok)
        return billChk;

    // Reload live stock; this order's own reservation is excluded by checkLines().
    const QHash<QString, qint64> stock = m_inv->physicalStock();
    const Result chk = checkLines(order, now, {}, stock);
    if (!chk.ok)
        return chk;

    QHash<QString, qint64> changes;
    for (const OrderLine &l : order.lines)
        changes[l.productId] = changes.value(l.productId, stock.value(l.productId)) - l.qtyMilli;

    bill.billId = nextBillId();
    if (bill.billId.isEmpty())
        return Result::failure(QStringLiteral("All nine-digit bill IDs have been used."));
    bill.paymentStatus = Status::Paid;
    if (!bill.billDate.isValid()) bill.billDate = now.date();
    if (!bill.billTime.isValid()) bill.billTime = now.time();
    if (!order.orderDate.isValid()) order.orderDate = now.date();
    if (!order.orderTime.isValid()) order.orderTime = now.time();
    order.status = Status::Paid;
    order.paidAt = now;
    order.holdExpiry = QDateTime();
    order.activeBillId = bill.billId;
    order.paymentPaisa = bill.totalPayable;
    bill.customerName = order.customerName;

    QMap<QString, OrderRecord> orders = m_orders;
    QMap<QString, BillRecord> bills = m_bills;
    orders[order.orderId] = order;
    bills[bill.billId] = bill;

    const Result r = commitAll(orders, bills, changes);
    if (r.ok && assignedBillId)
        *assignedBillId = bill.billId;
    return r;
}

Result CheckoutStorage::commitPaidRevision(OrderRecord revised, BillRecord replacement,
                                           const QDateTime &now, QString *assignedBillId)
{
    if (m_locked)
        return commitAll({}, {}, {});
    if (!m_inv || !now.isValid())
        return Result::failure(QStringLiteral("A paid revision requires a valid time and an inventory source."));

    // The window is checked again at save, using the original payment timestamp.
    const Result window = checkPaidEditWindow(revised.orderId, now);
    if (!window.ok)
        return window;
    const OrderRecord &old = m_orders[revised.orderId];

    const Result billChk = checkBillMatchesOrder(revised, replacement);
    if (!billChk.ok)
        return billChk;

    QHash<QString, qint64> oldQty, newQty;
    for (const OrderLine &l : old.lines) oldQty[l.productId] += l.qtyMilli;
    for (const OrderLine &l : revised.lines) newQty[l.productId] += l.qtyMilli;

    const QHash<QString, qint64> stock = m_inv->physicalStock();
    const Result chk = checkLines(revised, now, oldQty, stock); // revalidates only the added part
    if (!chk.ok)
        return chk;

    // physical stock changes by old quantity minus new quantity, by Product ID
    QHash<QString, qint64> changes;
    QSet<QString> ids;
    for (auto it = oldQty.cbegin(); it != oldQty.cend(); ++it) ids.insert(it.key());
    for (auto it = newQty.cbegin(); it != newQty.cend(); ++it) ids.insert(it.key());
    for (const QString &id : ids) {
        const qint64 delta = oldQty.value(id, 0) - newQty.value(id, 0);
        if (delta == 0) continue;
        if (!stock.contains(id))
            return Result::failure(QStringLiteral("Product %1 is not in the catalog.").arg(id));
        const qint64 ns = stock.value(id) + delta;
        if (ns < 0)
            return Result::failure(QStringLiteral("Stock for %1 would go negative.").arg(id));
        changes[id] = ns;
    }

    replacement.billId = nextBillId();
    if (replacement.billId.isEmpty())
        return Result::failure(QStringLiteral("All nine-digit bill IDs have been used."));
    replacement.paymentStatus = Status::Paid;
    replacement.customerName = revised.customerName;
    if (!replacement.billDate.isValid()) replacement.billDate = now.date();
    if (!replacement.billTime.isValid()) replacement.billTime = now.time();

    revised.status = Status::Paid;
    revised.paidAt = old.paidAt;               // original payment time never moves
    revised.orderDate = old.orderDate;
    revised.orderTime = old.orderTime;
    revised.holdExpiry = QDateTime();
    revised.activeBillId = replacement.billId;
    revised.paymentPaisa = replacement.totalPayable;

    QMap<QString, OrderRecord> orders = m_orders;
    QMap<QString, BillRecord> bills = m_bills;
    auto prev = bills.find(old.activeBillId);
    if (prev != bills.end()) {                  // keep history, only the new bill is active
        prev->superseded = true;
        prev->supersededBy = replacement.billId;
    }
    orders[revised.orderId] = revised;
    bills[replacement.billId] = replacement;

    const Result r = commitAll(orders, bills, changes);
    if (r.ok && assignedBillId)
        *assignedBillId = replacement.billId;
    return r;
}

} // namespace store

namespace shop {
namespace {

class CheckoutInventory final : public store::IInventory {
public:
    explicit CheckoutInventory(const QString& dataDir)
        : path_(QDir(dataDir).filePath(QStringLiteral("Product.txt")))
    {
    }

    bool load(ProductCatalog& catalog, QString* error) const
    {
        return catalog.load(path_, error);
    }

    QHash<QString, qint64> physicalStock() override
    {
        ProductCatalog catalog;
        QHash<QString, qint64> stock;
        if (load(catalog, nullptr)) {
            for (const Product& product : catalog.products())
                stock.insert(product.id, product.stockMilli);
        }
        return stock;
    }

    bool applyStock(const QHash<QString, qint64>& changes, QString* error) override
    {
        ProductCatalog catalog;
        if (!load(catalog, error))
            return false;
        for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
            if (!catalog.setStock(it.key(), it.value(), error))
                return false;
        }
        return catalog.save(path_, error);
    }

private:
    QString path_;
};

} // namespace

bool CheckoutStorage::save(const QString& dataDir, const Bill& bill,
                           const CheckoutDetails& details, const QDateTime& now,
                           CheckoutReceipt& receipt, QString* error)
{
    receipt = CheckoutReceipt{};
    if (error)
        error->clear();
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    if (dataDir.isEmpty() || !QDir(dataDir).exists() || !now.isValid())
        return fail(QStringLiteral("Checkout requires a valid data folder and time."));
    const QString customer = details.customerName.trimmed();
    if (customer.isEmpty() || details.status != store::Status::Paid || bill.lines.isEmpty())
        return fail(QStringLiteral("A paid checkout requires a customer and at least one item."));
    if (bill.totals.payablePaisa < 0 || details.paymentPaisa < bill.totals.payablePaisa)
        return fail(QStringLiteral("The payment amount does not cover the bill."));
    const QString mode = details.paymentMode.trimmed().toLower();
    const QString merchant = details.merchantName.trimmed();
    if (mode != QStringLiteral("cash") && mode != QStringLiteral("card") && mode != QStringLiteral("wallet"))
        return fail(QStringLiteral("Choose cash, card, or wallet."));
    if (mode != QStringLiteral("cash") && (merchant.isEmpty() || details.paymentPaisa != bill.totals.payablePaisa))
        return fail(QStringLiteral("Card and wallet payments require a provider and the exact payable amount."));

    QLockFile lock(QDir(dataDir).filePath(QStringLiteral(".checkout.lock")));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return fail(QStringLiteral("Another checkout operation is in progress. Try again."));

    CheckoutInventory inventory(dataDir);
    ProductCatalog catalog;
    if (!inventory.load(catalog, error))
        return false;

    store::CheckoutStorage storage(dataDir, &inventory);
    if (!storage.load()) {
        receipt.recoveryRequired = storage.isLocked();
        return fail(storage.loadErrors().join(QLatin1Char('\n')));
    }

    store::OrderRecord order;
    order.orderId = storage.nextOrderId(now.date());
    if (order.orderId.isEmpty())
        return fail(QStringLiteral("All order IDs have been used."));
    order.customerName = customer;
    order.contact = details.customerContact.trimmed();
    order.paymentMode = mode;
    order.paymentMerchant = mode == QStringLiteral("cash") ? QString() : merchant;

    store::BillRecord saved;
    saved.orderId = order.orderId;
    saved.totalBeforeTax = bill.totals.subtotalPaisa;
    saved.totalTax = bill.totals.taxPaisa;
    saved.totalProductDiscount = bill.totals.discountPaisa;
    saved.totalPayable = bill.totals.payablePaisa;
    saved.amountReceived = details.paymentPaisa;
    saved.balanceReturned = details.paymentPaisa - bill.totals.payablePaisa;
    saved.seasonalDiscount = bill.seasonalDiscountPaisa;
    saved.specialDiscount = bill.specialDiscountPaisa;

    for (const BillLine& line : bill.lines) {
        const Product& product = line.item.product;
        Product latest;
        if (!catalog.findById(product.id, latest) || latest.barcode != product.barcode
            || latest.unit != product.unit) {
            return fail(QStringLiteral("Product %1 changed in the catalog. Re-add it to the cart.")
                .arg(product.id));
        }

        const bool weighted = product.unit == Unit::Weight;
        order.lines.append(store::OrderLine{
            product.id, product.barcode, line.item.quantityMilli, weighted,
            product.acronym, product.pricePaisa, product.taxPaisa, product.discountPaisa
        });
        saved.lines.append(store::BillLine{
            product.acronym, product.pricePaisa, line.item.quantityMilli, weighted,
            line.amounts.discountPaisa
        });
    }

    QString billId;
    const store::Result result = storage.commitPaidSale(order, saved, now, &billId);
    receipt.recoveryRequired = result.recoveryFailed;
    if (!result.ok)
        return fail(result.error);
    receipt.orderId = order.orderId;
    receipt.billId = billId;
    return true;
}

} // namespace shop
