// ProductCatalog.cpp
#include "ProductCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>

#include <limits>
#include <utility>

namespace shop {

namespace {

constexpr qint64 kInt64Max = std::numeric_limits<qint64>::max();

void setError(QString* error, const QString& message)
{
    if (error) *error = message;
}

bool isAsciiDigit(QChar c)
{
    return c.unicode() >= '0' && c.unicode() <= '9';
}

// Parse "123", "123.4", "123.45" into an integer scaled by 10^scaleDigits.
// scaleDigits = 2 -> paisa, 3 -> thousandths.
bool parseScaled(const QString& text, int scaleDigits, bool allowFraction,
                 qint64& out, QString* error)
{
    const int n = static_cast<int>(text.size());
    if (n == 0) {
        setError(error, QStringLiteral("value is empty"));
        return false;
    }

    qint64 multiplier = 1;
    for (int k = 0; k < scaleDigits; ++k) multiplier *= 10;

    int i = 0;
    qint64 whole = 0;
    int wholeDigits = 0;
    while (i < n && isAsciiDigit(text.at(i))) {
        const qint64 d = text.at(i).unicode() - '0';
        if (whole > (kInt64Max - d) / 10) {
            setError(error, QStringLiteral("value \"%1\" is too large").arg(text));
            return false;
        }
        whole = whole * 10 + d;
        ++wholeDigits;
        ++i;
    }
    if (wholeDigits == 0) {
        setError(error, QStringLiteral("\"%1\" is not a non-negative number").arg(text));
        return false;
    }

    qint64 frac = 0;
    if (i < n && text.at(i) == QLatin1Char('.')) {
        if (!allowFraction) {
            setError(error, QStringLiteral("\"%1\" must be a whole number").arg(text));
            return false;
        }
        ++i;
        int fracDigits = 0;
        while (i < n && isAsciiDigit(text.at(i))) {
            if (fracDigits >= scaleDigits) {
                setError(error, QStringLiteral("\"%1\" has more than %2 decimal places")
                                    .arg(text).arg(scaleDigits));
                return false;
            }
            frac = frac * 10 + (text.at(i).unicode() - '0');
            ++fracDigits;
            ++i;
        }
        if (fracDigits == 0) {
            setError(error, QStringLiteral("\"%1\" has no digits after the decimal point").arg(text));
            return false;
        }
        for (; fracDigits < scaleDigits; ++fracDigits) frac *= 10;
    }
    if (i != n) {
        setError(error, QStringLiteral("\"%1\" is not a valid number").arg(text));
        return false;
    }

    if (whole > kInt64Max / multiplier || whole * multiplier > kInt64Max - frac) {
        setError(error, QStringLiteral("value \"%1\" is too large").arg(text));
        return false;
    }
    out = whole * multiplier + frac;
    return true;
}

// Rules every product must follow, whether it came from Product.txt or from
// the editor. The message does not include a line number.
bool checkProduct(const Product& p, QString& err)
{
    if (p.id.isEmpty()) { err = QStringLiteral("product ID is empty"); return false; }

    if (!isValidBarcodeText(p.barcode)) {
        err = QStringLiteral("barcode \"%1\" must be exactly six digits").arg(p.barcode);
        return false;
    }

    if (p.categoryId.size() != 2 || !isAsciiDigit(p.categoryId.at(0)) || !isAsciiDigit(p.categoryId.at(1))) {
        err = QStringLiteral("category ID \"%1\" must be exactly two digits").arg(p.categoryId);
        return false;
    }
    if (p.categoryId != p.barcode.left(2)) {
        err = QStringLiteral("category ID %1 must match the first two digits of barcode %2")
                  .arg(p.categoryId, p.barcode);
        return false;
    }

    if (p.acronym.isEmpty() || p.acronym.size() > 8) {
        err = QStringLiteral("acronym must be 1 to 8 characters, got \"%1\"").arg(p.acronym);
        return false;
    }

    if (p.name.isEmpty()) { err = QStringLiteral("product name is empty"); return false; }

    if (p.pricePaisa < 0 || p.taxPaisa < 0 || p.discountPaisa < 0 || p.stockMilli < 0) {
        err = QStringLiteral("price, tax, discount and quantity cannot be negative");
        return false;
    }
    if (p.pricePaisa > kInt64Max - p.taxPaisa || p.discountPaisa > p.pricePaisa + p.taxPaisa) {
        err = QStringLiteral("unit discount cannot exceed unit price plus tax");
        return false;
    }
    if (p.unit == Unit::Count && p.stockMilli % 1000 != 0) {
        err = QStringLiteral("a count product must have a whole-number quantity");
        return false;
    }
    return true;
}

// Turns one tokenized record into a product. Message has no line number
// (the caller adds it).
bool buildProduct(const QStringList& f, Product& p, QString& err)
{
    if (f.size() != 11) {
        err = QStringLiteral("expected 11 fields before #, found %1").arg(f.size());
        return false;
    }

    p.id = f.at(0);
    p.barcode = f.at(1);
    p.acronym = f.at(2);
    p.name = f.at(3);

    const QString unitText = f.at(4).toLower();
    if (unitText == QLatin1String("count")) {
        p.unit = Unit::Count;
    } else if (unitText == QLatin1String("weight")) {
        p.unit = Unit::Weight;
    } else {
        err = QStringLiteral("unit must be \"count\" or \"weight\", got \"%1\"").arg(f.at(4));
        return false;
    }

    QString detail;
    if (!parsePaisa(f.at(5), p.pricePaisa, &detail)) { err = QStringLiteral("unit price: ") + detail; return false; }
    if (!parsePaisa(f.at(6), p.taxPaisa, &detail)) { err = QStringLiteral("unit tax: ") + detail; return false; }
    if (!parsePaisa(f.at(7), p.discountPaisa, &detail)) { err = QStringLiteral("unit discount: ") + detail; return false; }
    if (!parseQuantityMilli(f.at(8), p.unit, p.stockMilli, &detail)) {
        err = QStringLiteral("available quantity: ") + detail;
        return false;
    }

    p.barcodePath = f.at(9);
    p.categoryId = f.at(10);
    return checkProduct(p, err);
}

} // namespace

// ---------------------------------------------------------------------------
// Shared record helpers (also used by CategoryCatalog)
// ---------------------------------------------------------------------------

// Split one record (terminator already removed) into whitespace-separated
// values. Double quotes group values that contain spaces; inside quotes
// \" is a quote and \\ is a backslash.
bool tokenizeRecord(const QString& s, QStringList& out, QString& err)
{
    const int n = static_cast<int>(s.size());
    int i = 0;
    while (true) {
        while (i < n && s.at(i).isSpace()) ++i;
        if (i >= n) break;

        if (s.at(i) == QLatin1Char('"')) {
            ++i;
            QString cur;
            bool closed = false;
            while (i < n) {
                const QChar c = s.at(i);
                if (c == QLatin1Char('\\')) {
                    if (i + 1 >= n) {
                        err = QStringLiteral("dangling backslash inside quoted value");
                        return false;
                    }
                    const QChar next = s.at(i + 1);
                    if (next == QLatin1Char('"') || next == QLatin1Char('\\')) {
                        cur += next;
                        i += 2;
                    } else {
                        err = QStringLiteral("unsupported escape \\%1 in quoted value "
                                             "(use \\\" or \\\\, and forward slashes in paths)")
                                  .arg(next);
                        return false;
                    }
                } else if (c == QLatin1Char('"')) {
                    closed = true;
                    ++i;
                    break;
                } else {
                    cur += c;
                    ++i;
                }
            }
            if (!closed) {
                err = QStringLiteral("unterminated quoted value");
                return false;
            }
            if (i < n && !s.at(i).isSpace()) {
                err = QStringLiteral("unexpected text right after a closing quote");
                return false;
            }
            out << cur;
        } else {
            const int start = i;
            while (i < n && !s.at(i).isSpace()) {
                if (s.at(i) == QLatin1Char('"')) {
                    err = QStringLiteral("quote inside an unquoted value (quote the whole value)");
                    return false;
                }
                ++i;
            }
            out << s.mid(start, i - start);
        }
    }
    return true;
}

// Quote when forced, empty, or containing whitespace / quote / backslash.
QString quoteField(const QString& value, bool force)
{
    bool need = force || value.isEmpty();
    for (const QChar c : value) {
        if (c.isSpace() || c == QLatin1Char('"') || c == QLatin1Char('\\')) {
            need = true;
            break;
        }
    }
    if (!need) return value;

    QString out;
    out.reserve(static_cast<int>(value.size()) + 2);
    out += QLatin1Char('"');
    for (const QChar c : value) {
        if (c == QLatin1Char('"') || c == QLatin1Char('\\')) out += QLatin1Char('\\');
        out += c;
    }
    out += QLatin1Char('"');
    return out;
}

// ---------------------------------------------------------------------------
// Number helpers
// ---------------------------------------------------------------------------

bool isValidBarcodeText(const QString& barcode)
{
    // NOTE: the guide says "the shared validator" from Increment 1. If that
    // class/function exists in your project, call it here instead so there is
    // a single definition. The rule implemented: exactly six ASCII digits.
    if (barcode.size() != 6) return false;
    for (const QChar c : barcode) {
        if (!isAsciiDigit(c)) return false;
    }
    return true;
}

bool parsePaisa(const QString& text, qint64& paisa, QString* error)
{
    qint64 value = 0;
    QString detail;
    if (!parseScaled(text, 2, true, value, &detail)) {
        setError(error, detail);
        return false;
    }
    paisa = value;
    return true;
}

bool parseQuantityMilli(const QString& text, Unit unit, qint64& quantityMilli, QString* error)
{
    qint64 value = 0;
    QString detail;
    if (unit == Unit::Count) {
        qint64 whole = 0;
        if (!parseScaled(text, 0, false, whole, &detail)) {
            setError(error, detail);
            return false;
        }
        if (whole > kInt64Max / 1000) {
            setError(error, QStringLiteral("value \"%1\" is too large").arg(text));
            return false;
        }
        value = whole * 1000;
    } else if (!parseScaled(text, 3, true, value, &detail)) {
        setError(error, detail);
        return false;
    }
    quantityMilli = value;
    return true;
}

QString formatPaisa(qint64 paisa)
{
    const bool negative = paisa < 0;
    const quint64 magnitude = negative ? (~static_cast<quint64>(paisa) + 1u)
                                       : static_cast<quint64>(paisa);
    return QStringLiteral("%1%2.%3")
        .arg(negative ? QStringLiteral("-") : QString())
        .arg(static_cast<qulonglong>(magnitude / 100))
        .arg(static_cast<qulonglong>(magnitude % 100), 2, 10, QLatin1Char('0'));
}

QString formatQuantityMilli(qint64 quantityMilli, Unit unit)
{
    if (unit == Unit::Count && quantityMilli % 1000 == 0)
        return QString::number(quantityMilli / 1000);

    const bool negative = quantityMilli < 0;
    const quint64 magnitude = negative ? (~static_cast<quint64>(quantityMilli) + 1u)
                                       : static_cast<quint64>(quantityMilli);
    return QStringLiteral("%1%2.%3")
        .arg(negative ? QStringLiteral("-") : QString())
        .arg(static_cast<qulonglong>(magnitude / 1000))
        .arg(static_cast<qulonglong>(magnitude % 1000), 3, 10, QLatin1Char('0'));
}

// ---------------------------------------------------------------------------
// ProductCatalog
// ---------------------------------------------------------------------------

bool ProductCatalog::load(const QString& productPath, QString* error)
{
    const QFileInfo info(productPath);
    const QString fileName = info.fileName();

    QFile file(productPath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot open %1: %2").arg(productPath, file.errorString()));
        return false;
    }
    QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        setError(error, QStringLiteral("Cannot read %1: %2").arg(productPath, file.errorString()));
        return false;
    }
    file.close();

    if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);   // UTF-8 BOM

    // Build everything in temporaries; products_ is replaced only if the
    // whole file passes.
    QHash<QString, Product> products;
    QVector<QString> order;
    QHash<QString, int> idLine;       // product ID -> line number
    QHash<QString, int> barcodeLine;  // barcode    -> line number

    const QList<QByteArray> rawLines = bytes.split('\n');
    for (int index = 0; index < rawLines.size(); ++index) {
        const int lineNo = index + 1;
        const auto fail = [&](const QString& message) {
            setError(error, QStringLiteral("%1 line %2: %3").arg(fileName).arg(lineNo).arg(message));
            return false;
        };

        const QString decoded = QString::fromUtf8(rawLines.at(index));
        if (decoded.contains(QChar(QChar::ReplacementCharacter)))
            return fail(QStringLiteral("invalid UTF-8 text"));

        QString line = decoded.trimmed();   // also drops a trailing '\r'
        if (line.isEmpty()) continue;

        if (!line.endsWith(QLatin1Char('#')))
            return fail(QStringLiteral("record must end with #"));
        line.chop(1);

        QStringList fields;
        QString problem;
        if (!tokenizeRecord(line, fields, problem)) return fail(problem);

        Product product;
        if (!buildProduct(fields, product, problem)) return fail(problem);

        if (idLine.contains(product.id))
            return fail(QStringLiteral("duplicate product ID \"%1\" (first used on line %2)")
                            .arg(product.id).arg(idLine.value(product.id)));
        if (barcodeLine.contains(product.barcode))
            return fail(QStringLiteral("duplicate barcode %1 (first used on line %2)")
                            .arg(product.barcode).arg(barcodeLine.value(product.barcode)));

        idLine.insert(product.id, lineNo);
        barcodeLine.insert(product.barcode, lineNo);
        order.append(product.barcode);
        products.insert(product.barcode, product);
    }

    if (products.isEmpty()) {
        setError(error, QStringLiteral("%1 contains no product records").arg(fileName));
        return false;
    }

    // Publish.
    products_ = std::move(products);
    order_ = std::move(order);
    baseDir_ = info.absoluteDir().absolutePath();
    filePath_ = info.absoluteFilePath();
    if (error) error->clear();
    return true;
}

bool ProductCatalog::findByBarcode(const QString& barcode, Product& product) const
{
    const auto it = products_.constFind(barcode);
    if (it == products_.constEnd()) return false;
    product = it.value();
    return true;
}

bool ProductCatalog::findById(const QString& id, Product& product) const
{
    for (const QString& key : order_) {
        const Product& candidate = products_.value(key);
        if (candidate.id == id) {
            product = candidate;
            return true;
        }
    }
    return false;
}

QVector<Product> ProductCatalog::products() const
{
    QVector<Product> list;
    list.reserve(order_.size());
    for (const QString& key : order_) list.append(products_.value(key));
    return list;
}

int ProductCatalog::size() const
{
    return static_cast<int>(products_.size());
}

QString ProductCatalog::baseDirectory() const
{
    return baseDir_;
}

QString ProductCatalog::resolveBarcodePath(const Product& product) const
{
    if (product.barcodePath.isEmpty()) return QString();
    if (QDir::isAbsolutePath(product.barcodePath))
        return QDir::cleanPath(product.barcodePath);
    return QDir::cleanPath(baseDir_ + QLatin1Char('/') + product.barcodePath);
}

bool ProductCatalog::deductStock(const QString& productId, qint64 quantityMilli, QString* error)
{
    if (quantityMilli <= 0) {
        setError(error, QStringLiteral("quantity to deduct must be positive"));
        return false;
    }
    for (const QString& key : order_) {
        Product& product = products_[key];
        if (product.id != productId) continue;
        if (quantityMilli > product.stockMilli) {
            setError(error, QStringLiteral("not enough stock for %1: have %2, need %3")
                                .arg(product.name,
                                     formatQuantityMilli(product.stockMilli, product.unit),
                                     formatQuantityMilli(quantityMilli, product.unit)));
            return false;
        }
        product.stockMilli -= quantityMilli;
        if (error) error->clear();
        return true;
    }
    setError(error, QStringLiteral("unknown product ID \"%1\"").arg(productId));
    return false;
}

QByteArray ProductCatalog::serialize() const
{
    QString text;
    for (const QString& key : order_) {
        const Product& p = products_.value(key);
        const QStringList fields = {
            quoteField(p.id, false),
            quoteField(p.barcode, false),
            quoteField(p.acronym, false),
            quoteField(p.name, true),
            p.unit == Unit::Weight ? QStringLiteral("weight") : QStringLiteral("count"),
            formatPaisa(p.pricePaisa),
            formatPaisa(p.taxPaisa),
            formatPaisa(p.discountPaisa),
            formatQuantityMilli(p.stockMilli, p.unit),
            quoteField(p.barcodePath, false),
            quoteField(p.categoryId, false),
            QStringLiteral("#"),
        };
        text += fields.join(QLatin1Char(' '));
        text += QLatin1Char('\n');
    }
    return text.toUtf8();
}

bool ProductCatalog::save(const QString& productPath, QString* error) const
{
    QSaveFile file(productPath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("Cannot write %1: %2").arg(productPath, file.errorString()));
        return false;
    }
    const QByteArray data = serialize();
    if (file.write(data) != data.size() || !file.commit()) {
        setError(error, QStringLiteral("Cannot write %1: %2").arg(productPath, file.errorString()));
        return false;
    }
    if (error) error->clear();
    return true;
}

// ---------------------------------------------------------------------------
// Increment 3: manager features
// ---------------------------------------------------------------------------

QString ProductCatalog::filePath() const
{
    return filePath_;
}

QString ProductCatalog::nextBarcode(const QString& categoryId) const
{
    if (categoryId.size() != 2 || !isAsciiDigit(categoryId.at(0)) || !isAsciiDigit(categoryId.at(1)))
        return QString();

    for (int serial = 1; serial <= 9999; ++serial) {
        const QString candidate = categoryId + QStringLiteral("%1").arg(serial, 4, 10, QLatin1Char('0'));
        if (!products_.contains(candidate)) return candidate;
    }
    return QString();   // all 9999 numbers of this category are used
}

bool ProductCatalog::addProduct(const Product& product, QString* error)
{
    QString problem;
    if (!checkProduct(product, problem)) {
        setError(error, problem);
        return false;
    }
    if (products_.contains(product.barcode)) {
        setError(error, QStringLiteral("barcode %1 is already used").arg(product.barcode));
        return false;
    }
    Product existing;
    if (findById(product.id, existing)) {
        setError(error, QStringLiteral("product ID \"%1\" is already used").arg(product.id));
        return false;
    }

    products_.insert(product.barcode, product);
    order_.append(product.barcode);
    if (error) error->clear();
    return true;
}

bool ProductCatalog::updateProduct(const Product& product, QString* error)
{
    Product existing;
    if (!findById(product.id, existing)) {
        setError(error, QStringLiteral("unknown product ID \"%1\"").arg(product.id));
        return false;
    }
    if (product.barcode != existing.barcode || product.categoryId != existing.categoryId) {
        setError(error, QStringLiteral("the barcode and category of an existing product cannot be changed"));
        return false;
    }

    QString problem;
    if (!checkProduct(product, problem)) {
        setError(error, problem);
        return false;
    }

    products_[product.barcode] = product;
    if (error) error->clear();
    return true;
}

bool ProductCatalog::setStock(const QString& productId, qint64 quantityMilli, QString* error)
{
    if (quantityMilli < 0) {
        setError(error, QStringLiteral("available quantity cannot be negative"));
        return false;
    }
    for (const QString& key : order_) {
        Product& product = products_[key];
        if (product.id != productId) continue;
        if (product.unit == Unit::Count && quantityMilli % 1000 != 0) {
            setError(error, QStringLiteral("%1 is counted in whole units").arg(product.name));
            return false;
        }
        product.stockMilli = quantityMilli;
        if (error) error->clear();
        return true;
    }
    setError(error, QStringLiteral("unknown product ID \"%1\"").arg(productId));
    return false;
}

} // namespace shop
