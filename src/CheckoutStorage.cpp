// CheckoutStorage.cpp
#include "CheckoutStorage.h"
#include "ProductCatalog.h"
#include "Order.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QVector>
#include <limits>

namespace shop {
namespace {

// ---- Provisional rules (confirm with the teacher) --------------------------
const QStringList kPaymentModes = {
    QStringLiteral("cash"), QStringLiteral("card"), QStringLiteral("online")};
const QStringList kStatuses = {
    QStringLiteral("in-progress"), QStringLiteral("paid")};

constexpr qint64 kMaxOrderSeq = 999999;
constexpr qint64 kMaxBillSeq  = 999999999;

const char* const kOrderFile  = "Order.txt";
const char* const kDetailFile = "OrderDetail.txt";
const char* const kBillFile   = "Bill.txt";
const char* const kProductFile = "Product.txt";

// ---- Helpers ----------------------------------------------------------------
bool fail(QString* error, const QString& msg) {
    if (error) *error = msg;
    return false;
}

// Integer paisa -> "565.00"
QString formatMoney(qint64 paisa) {
    const bool neg = paisa < 0;
    const quint64 a = neg ? quint64(-(paisa + 1)) + 1u : quint64(paisa);
    return QStringLiteral("%1%2.%3")
        .arg(neg ? QStringLiteral("-") : QString())
        .arg(a / 100)
        .arg(a % 100, 2, 10, QLatin1Char('0'));
}

// Integer thousandths -> "0.500"
QString formatMilli3(qint64 milli) {
    return QStringLiteral("%1.%2")
        .arg(milli / 1000)
        .arg(milli % 1000, 3, 10, QLatin1Char('0'));
}

// Count -> whole units ("2"), weight -> decimal kg ("0.500").
// Share this formatter with Anaya so Product.txt stays consistent.
QString formatQuantity(Unit unit, qint64 milli) {
    return unit == Unit::Count ? QString::number(milli / 1000)
                               : formatMilli3(milli);
}

// Quote text, escaping backslash and double quote.
QString quoted(const QString& s) {
    QString out;
    out.reserve(s.size() + 2);
    out += QLatin1Char('"');
    for (const QChar c : s) {
        if (c == QLatin1Char('"') || c == QLatin1Char('\\'))
            out += QLatin1Char('\\');
        out += c;
    }
    out += QLatin1Char('"');
    return out;
}

bool isAllDigits(const QString& s) {
    if (s.isEmpty()) return false;
    for (const QChar c : s)
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
    return true;
}

bool addOverflows(qint64 a, qint64 b) {
    return (b > 0 && a > std::numeric_limits<qint64>::max() - b) ||
           (b < 0 && a < std::numeric_limits<qint64>::min() - b);
}

// Read a whole file. A missing file is not an error (exists = false).
bool readAll(const QString& path, QByteArray& bytes, bool& exists,
             QString* error) {
    bytes.clear();
    exists = QFile::exists(path);
    if (!exists) return true;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("%1: cannot read (%2)")
                               .arg(QFileInfo(path).fileName(), f.errorString()));
    bytes = f.readAll();
    return true;
}

QStringList splitLines(const QByteArray& bytes) {
    QStringList lines = QString::fromUtf8(bytes).split(QLatin1Char('\n'));
    for (QString& l : lines)
        if (l.endsWith(QLatin1Char('\r'))) l.chop(1);
    return lines;
}

// Split a line on whitespace, keeping quoted tokens (with escapes) as raw text.
bool tokenizeRaw(const QString& line, QStringList& tokens) {
    tokens.clear();
    int i = 0;
    const int n = line.size();
    while (i < n) {
        if (line[i].isSpace()) { ++i; continue; }
        QString tok;
        bool inQuote = false;
        while (i < n && (inQuote || !line[i].isSpace())) {
            const QChar c = line[i];
            if (inQuote && c == QLatin1Char('\\') && i + 1 < n) {
                tok += c; tok += line[i + 1]; i += 2; continue;
            }
            if (c == QLatin1Char('"')) inQuote = !inQuote;
            tok += c;
            ++i;
        }
        if (inQuote) return false;  // unterminated quote
        tokens << tok;
    }
    return true;
}

// Largest existing sequence in a file whose first token is an ID of idLen
// digits; the sequence is the first seqLen digits. Returns the next one.
bool nextSequence(const QByteArray& bytes, const QString& fileName, int idLen,
                  int seqLen, qint64 maxSeq, qint64& next, QString* error) {
    qint64 maxFound = 0;
    const QStringList lines = splitLines(bytes);
    for (int i = 0; i < lines.size(); ++i) {
        const QString t = lines[i].trimmed();
        if (t.isEmpty()) continue;
        const QString id = t.section(QRegularExpression(QStringLiteral("\\s")), 0, 0);
        if (id.size() != idLen || !isAllDigits(id))
            return fail(error, QStringLiteral("%1 line %2: invalid ID")
                                   .arg(fileName).arg(i + 1));
        maxFound = qMax(maxFound, id.left(seqLen).toLongLong());
    }
    if (maxFound >= maxSeq)
        return fail(error, QStringLiteral("%1: ID range exhausted").arg(fileName));
    next = maxFound + 1;
    return true;
}

bool validateBill(const Bill& bill, const CheckoutDetails& d,
                  QString* error) {
    if (bill.lines.isEmpty())
        return fail(error, QStringLiteral("Bill has no lines"));
    if (d.customerName.trimmed().isEmpty())
        return fail(error, QStringLiteral("Customer name is required"));
    if (!kPaymentModes.contains(d.paymentMode))
        return fail(error, QStringLiteral("Unsupported payment mode: %1")
                               .arg(d.paymentMode));
    if (!kStatuses.contains(d.status))
        return fail(error, QStringLiteral("Unsupported status: %1").arg(d.status));

    Amounts sum;
    for (const BillLine& l : bill.lines) {
        const Product& p = l.item.product;
        if (p.id.isEmpty() || !isAllDigits(p.barcode) || p.barcode.size() != 6 ||
            p.acronym.isEmpty())
            return fail(error, QStringLiteral("Bill line has an invalid product"));
        if (l.item.quantityMilli <= 0)
            return fail(error, QStringLiteral("Bill line %1 has invalid quantity")
                                   .arg(p.id));
        const Amounts& a = l.amounts;
        if (a.subtotalPaisa < 0 || a.taxPaisa < 0 || a.discountPaisa < 0)
            return fail(error, QStringLiteral("Bill line %1 has negative amounts")
                                   .arg(p.id));
        if (addOverflows(sum.subtotalPaisa, a.subtotalPaisa) ||
            addOverflows(sum.taxPaisa, a.taxPaisa) ||
            addOverflows(sum.discountPaisa, a.discountPaisa))
            return fail(error, QStringLiteral("Bill amounts overflow"));
        sum.subtotalPaisa += a.subtotalPaisa;
        sum.taxPaisa += a.taxPaisa;
        sum.discountPaisa += a.discountPaisa;
    }
    const Amounts& t = bill.totals;
    if (sum.subtotalPaisa != t.subtotalPaisa || sum.taxPaisa != t.taxPaisa ||
        sum.discountPaisa != t.discountPaisa)
        return fail(error, QStringLiteral("Bill totals do not match its lines"));
    if (addOverflows(t.subtotalPaisa, t.taxPaisa) ||
        t.subtotalPaisa + t.taxPaisa - t.discountPaisa != t.payablePaisa ||
        t.payablePaisa < 0)
        return fail(error, QStringLiteral("Bill payable is inconsistent"));
    if (d.status == QLatin1String("paid") && d.paymentPaisa != t.payablePaisa)
        return fail(error, QStringLiteral("Payment amount must equal payable"));
    return true;
}

struct PendingFile {
    QString name;        // e.g. "Order.txt"
    QString path;
    QByteArray oldBytes;
    bool existed = false;
    QByteArray newBytes;
};

QByteArray appended(const QByteArray& old, const QString& rows) {
    QByteArray out = old;
    if (!out.isEmpty() && !out.endsWith('\n')) out += '\n';
    out += rows.toUtf8();
    return out;
}

bool commitFile(const PendingFile& f, QString* error) {
    QSaveFile sf(f.path);
    if (!sf.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("%1: cannot open for writing (%2)")
                               .arg(f.name, sf.errorString()));
    if (sf.write(f.newBytes) != f.newBytes.size()) {
        sf.cancelWriting();
        return fail(error, QStringLiteral("%1: write failed (%2)")
                               .arg(f.name, sf.errorString()));
    }
    if (!sf.commit())
        return fail(error, QStringLiteral("%1: commit failed (%2)")
                               .arg(f.name, sf.errorString()));
    return true;
}

bool restoreFile(const PendingFile& f) {
    if (!f.existed) return !QFile::exists(f.path) || QFile::remove(f.path);
    QSaveFile sf(f.path);
    if (!sf.open(QIODevice::WriteOnly)) return false;
    if (sf.write(f.oldBytes) != f.oldBytes.size()) {
        sf.cancelWriting();
        return false;
    }
    return sf.commit();
}

// Rewrite Product.txt, replacing only the stock field (index 8) of the
// products in newStock. The other fields are kept exactly as read.
bool rewriteProducts(const QByteArray& oldBytes,
                     const QHash<QString, Product>& updated,
                     QByteArray& out, QString* error) {
    const QStringList lines = splitLines(oldBytes);
    QStringList result;
    QHash<QString, bool> seen;
    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines[i];
        if (line.trimmed().isEmpty()) {
            // keep blank lines, but drop the artificial one after the last '\n'
            if (i != lines.size() - 1) result << line;
            continue;
        }
        QStringList tok;
        if (!tokenizeRaw(line, tok) || tok.size() != 11 ||
            tok.last() != QLatin1String("#"))
            return fail(error, QStringLiteral("Product.txt line %1: malformed")
                                   .arg(i + 1));
        const auto it = updated.constFind(tok[0]);
        if (it != updated.constEnd()) {
            tok[8] = formatQuantity(it->unit, it->stockMilli);
            seen[tok[0]] = true;
        }
        result << tok.join(QLatin1Char(' '));
    }
    for (auto it = updated.constBegin(); it != updated.constEnd(); ++it)
        if (!seen.contains(it.key()))
            return fail(error, QStringLiteral("Product.txt: product %1 not found")
                                   .arg(it.key()));
    out = (result.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
    return true;
}

}  // namespace

bool CheckoutStorage::save(const QString& dataDir, const Bill& bill,
                           const CheckoutDetails& details, const QDateTime& now,
                           CheckoutReceipt& receipt, QString* error) {
    receipt = CheckoutReceipt{};  // clear at entry
    if (error) error->clear();

    // 1. Validate input -------------------------------------------------------
    if (dataDir.isEmpty())
        return fail(error, QStringLiteral("Data folder is not set"));
    if (!now.isValid())
        return fail(error, QStringLiteral("Invalid checkout time"));
    if (!validateBill(bill, details, error)) return false;

    QDir dir(dataDir);
    if (!dir.exists() && !QDir().mkpath(dataDir))
        return fail(error, QStringLiteral("Cannot create data folder"));

    PendingFile order{QString::fromLatin1(kOrderFile),
                      dir.filePath(QString::fromLatin1(kOrderFile))};
    PendingFile detail{QString::fromLatin1(kDetailFile),
                       dir.filePath(QString::fromLatin1(kDetailFile))};
    PendingFile billF{QString::fromLatin1(kBillFile),
                      dir.filePath(QString::fromLatin1(kBillFile))};
    PendingFile product{QString::fromLatin1(kProductFile),
                        dir.filePath(QString::fromLatin1(kProductFile))};

    // 2. Read existing records and assign IDs (not published yet) -------------
    if (!readAll(order.path, order.oldBytes, order.existed, error)) return false;
    if (!readAll(detail.path, detail.oldBytes, detail.existed, error)) return false;
    if (!readAll(billF.path, billF.oldBytes, billF.existed, error)) return false;
    if (!readAll(product.path, product.oldBytes, product.existed, error)) return false;
    if (!product.existed)
        return fail(error, QStringLiteral("Product.txt: file not found"));

    qint64 orderSeq = 0, billSeq = 0;
    if (!nextSequence(order.oldBytes, order.name, 8, 6, kMaxOrderSeq, orderSeq, error))
        return false;
    if (!nextSequence(billF.oldBytes, billF.name, 9, 9, kMaxBillSeq, billSeq, error))
        return false;

    const QString orderId = QStringLiteral("%1%2")
                                .arg(orderSeq, 6, 10, QLatin1Char('0'))
                                .arg(now.date().day(), 2, 10, QLatin1Char('0'));
    const QString billId = QStringLiteral("%1").arg(billSeq, 9, 10, QLatin1Char('0'));

    // 3. Recheck stock against the latest Product.txt -------------------------
    ProductCatalog catalog;
    QString catErr;
    if (!catalog.load(product.path, &catErr))
        return fail(error, QStringLiteral("Product.txt: %1").arg(catErr));

    Order temp;
    for (const BillLine& l : bill.lines) {
        Product latest;
        if (!catalog.findByBarcode(l.item.product.barcode, latest) ||
            latest.id != l.item.product.id)
            return fail(error, QStringLiteral("Product %1 (%2) no longer matches the catalog")
                                   .arg(l.item.product.id, l.item.product.barcode));
        QString addErr;
        if (!temp.add(latest, l.item.quantityMilli, &addErr))
            return fail(error, QStringLiteral("Stock check failed for %1: %2")
                                   .arg(latest.id, addErr));
    }
    // Apply deductions to a copy (id -> product with new stock).
    QHash<QString, Product> updated;
    for (const OrderItem& it : temp.items()) {
        Product p = it.product;
        if (it.quantityMilli > p.stockMilli)
            return fail(error, QStringLiteral("Insufficient stock for %1").arg(p.id));
        p.stockMilli -= it.quantityMilli;
        updated.insert(p.id, p);
    }

    // 4. Serialize records ----------------------------------------------------
    const QString orderRow = QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8 %9 #\n")
        .arg(orderId,
             now.toString(QStringLiteral("yyyy-MM-dd")),
             now.toString(QStringLiteral("HH:mm:ss")),
             quoted(details.customerName.trimmed()),
             quoted(details.customerContact.trimmed()),
             details.status,
             formatMoney(details.paymentPaisa),
             details.paymentMode,
             quoted(details.merchantName.trimmed()));

    QString detailRows;
    QStringList billItems;
    for (const BillLine& l : bill.lines) {
        const Product& p = l.item.product;
        const QString qty = formatQuantity(p.unit, l.item.quantityMilli);
        detailRows += QStringLiteral("%1 %2 %3 %4\n").arg(orderId, p.id, p.barcode, qty);
        billItems << QStringLiteral("%1 %2 %3 %4")
                         .arg(p.acronym, formatMoney(p.pricePaisa),
                              formatMoney(p.discountPaisa), qty);
    }
    const Amounts& t = bill.totals;
    const QString billRow = QStringLiteral("%1 %2 %3 %4 %5 %6 %7 %8 %9 #\n")
        .arg(billId, quoted(details.customerName.trimmed()), orderId,
             billItems.join(QLatin1Char(' ')),
             formatMoney(t.subtotalPaisa), formatMoney(t.taxPaisa),
             formatMoney(t.discountPaisa), formatMoney(t.payablePaisa),
             details.status);

    order.newBytes = appended(order.oldBytes, orderRow);
    detail.newBytes = appended(detail.oldBytes, detailRows);
    billF.newBytes = appended(billF.oldBytes, billRow);
    if (!rewriteProducts(product.oldBytes, updated, product.newBytes, error))
        return false;

    // 5. Commit with rollback -------------------------------------------------
    QVector<PendingFile*> files{&order, &detail, &billF, &product};
    for (int i = 0; i < files.size(); ++i) {
        QString commitErr;
        if (commitFile(*files[i], &commitErr)) continue;

        // The failing QSaveFile leaves its target untouched; restore earlier ones.
        QStringList restoreFailures;
        for (int j = i - 1; j >= 0; --j)
            if (!restoreFile(*files[j])) restoreFailures << files[j]->name;

        QString msg = commitErr;
        if (!restoreFailures.isEmpty()) {
            receipt.recoveryRequired = true;
            msg += QStringLiteral(" | Could not restore: %1. Check the data files "
                                  "before retrying.")
                       .arg(restoreFailures.join(QStringLiteral(", ")));
        }
        return fail(error, msg);
    }

    // 6. Publish IDs only after every write succeeded -------------------------
    receipt.orderId = orderId;
    receipt.billId = billId;
    receipt.recoveryRequired = false;
    return true;
}

}  // namespace shop
