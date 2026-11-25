// ProductCatalogTests.cpp - console tests for Anaya's catalog (QtCore only).
// Build: see tests/CMakeLists.txt.  Run: ProductCatalogTests <path-to-data-folder>
// Exit code 0 = all checks passed.
#include "CategoryCatalog.h"
#include "ProductCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>

using namespace shop;

static int g_failed = 0;
static int g_passed = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (cond) { ++g_passed; }                                                       \
        else { ++g_failed; std::printf("  FAIL line %d: %s\n", __LINE__, #cond); }       \
    } while (0)

#define CHECK_CONTAINS(text, part)                                                      \
    do {                                                                                \
        if ((text).contains(QLatin1String(part))) { ++g_passed; }                       \
        else {                                                                          \
            ++g_failed;                                                                 \
            std::printf("  FAIL line %d: \"%s\" does not contain \"%s\"\n", __LINE__,   \
                        (text).toUtf8().constData(), part);                             \
        }                                                                               \
    } while (0)

static bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(bytes) == bytes.size();
}

static const char* kGoodRows =
    "P001 123456 MILK \"Whole Milk\" count 200.00 10.00 5.00 10 barcodes/123456.png 12 #\n"
    "P002 987654 RICE \"White Rice\" weight 300.00 20.00 10.00 5.000 barcodes/987654.png 98 #\n";

// Loads `content` as Product.txt in a fresh temp dir; returns error text ("" if it loaded).
static QString loadText(const QByteArray& content, ProductCatalog* out = nullptr)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("Product.txt"));
    writeFile(path, content);
    ProductCatalog local;
    ProductCatalog& catalog = out ? *out : local;
    QString error;
    if (catalog.load(path, &error)) return QString();
    return error.isEmpty() ? QStringLiteral("<no message>") : error;
}

static void testSampleFile(const QString& dataDir)
{
    std::printf("sample data/Product.txt\n");
    ProductCatalog catalog;
    QString error;
    CHECK(catalog.load(dataDir + QStringLiteral("/Product.txt"), &error));
    CHECK(error.isEmpty());
    CHECK(catalog.size() == 15);

    Product p;
    CHECK(catalog.findByBarcode(QStringLiteral("123456"), p));
    CHECK(p.name == QStringLiteral("Whole Milk"));
    CHECK(p.id == QStringLiteral("P001"));
    CHECK(p.acronym == QStringLiteral("MILK"));
    CHECK(p.unit == Unit::Count);
    CHECK(p.pricePaisa == 20000 && p.taxPaisa == 1000 && p.discountPaisa == 500);
    CHECK(p.stockMilli == 10000);
    CHECK(p.barcodePath == QStringLiteral("barcodes/123456.png"));

    CHECK(catalog.findByBarcode(QStringLiteral("987654"), p));
    CHECK(p.name == QStringLiteral("White Rice"));
    CHECK(p.unit == Unit::Weight);
    CHECK(p.pricePaisa == 30000 && p.taxPaisa == 2000 && p.discountPaisa == 1000);
    CHECK(p.stockMilli == 5000);

    Product untouched;
    untouched.name = QStringLiteral("sentinel");
    CHECK(!catalog.findByBarcode(QStringLiteral("000000"), untouched));
    CHECK(untouched.name == QStringLiteral("sentinel"));   // not overwritten on a miss
    CHECK(!catalog.findByBarcode(QString(), untouched));
    CHECK(!catalog.findByBarcode(QStringLiteral("12345"), untouched));

    // The demo PNGs exist where the relative path says they should.
    CHECK(catalog.findByBarcode(QStringLiteral("123456"), p));
    CHECK(QFileInfo::exists(catalog.resolveBarcodePath(p)));
    CHECK(catalog.findByBarcode(QStringLiteral("987654"), p));
    CHECK(QFileInfo::exists(catalog.resolveBarcodePath(p)));

    Product byId;
    CHECK(catalog.findById(QStringLiteral("P002"), byId) && byId.barcode == QStringLiteral("987654"));
    CHECK(catalog.findById(QStringLiteral("P006"), byId) && byId.name == QStringLiteral("Sindhri Mangoes") && byId.unit == Unit::Weight);

    // PCategory.txt: every product's category exists, and the barcodes start with it.
    CategoryCatalog cats;
    CHECK(cats.load(dataDir + QStringLiteral("/PCategory.txt"), &error));
    CHECK(cats.categories().size() == 4);
    const QVector<Product> all = catalog.products();
    for (const Product& item : all) {
        Category c;
        CHECK(cats.findById(item.categoryId, c));
    }
    CHECK(!catalog.findById(QStringLiteral("P999"), byId));
}

static void testRejections()
{
    std::printf("invalid files report the right line\n");
    QString e;

    // Duplicate barcode on line 2.
    e = loadText(
        "P001 123456 MILK \"Whole Milk\" count 200.00 10.00 5.00 10 a.png 12 #\n"
        "P002 123456 RICE \"White Rice\" count 300.00 20.00 10.00 5 b.png 12 #\n");
    CHECK_CONTAINS(e, "line 2");
    CHECK_CONTAINS(e, "duplicate barcode");

    // Duplicate ID on line 3 (blank line 2 still counts).
    e = loadText(
        "P001 123456 MILK \"Whole Milk\" count 200.00 10.00 5.00 10 a.png 12 #\n"
        "\n"
        "P001 987654 RICE \"White Rice\" count 300.00 20.00 10.00 5 b.png 98 #\n");
    CHECK_CONTAINS(e, "line 3");
    CHECK_CONTAINS(e, "duplicate product ID");

    // Bad measurement unit on line 1.
    e = loadText("P001 123456 MILK \"Whole Milk\" litre 200.00 10.00 5.00 10 a.png 12 #\n");
    CHECK_CONTAINS(e, "line 1");
    CHECK_CONTAINS(e, "unit");

    // Missing # on line 2.
    e = loadText(
        "P001 123456 MILK \"Whole Milk\" count 200.00 10.00 5.00 10 a.png 12 #\n"
        "P002 987654 RICE \"White Rice\" weight 300.00 20.00 10.00 5.000 b.png\n");
    CHECK_CONTAINS(e, "line 2");
    CHECK_CONTAINS(e, "#");

    // Barcode problems.
    CHECK(!loadText("P001 12345 MILK \"M\" count 1.00 0 0 1 a.png 12 #\n").isEmpty());    // 5 digits
    CHECK(!loadText("P001 1234567 MILK \"M\" count 1.00 0 0 1 a.png 12 #\n").isEmpty());  // 7 digits
    CHECK(!loadText("P001 12A456 MILK \"M\" count 1.00 0 0 1 a.png 12 #\n").isEmpty());   // letter

    // Acronym: empty (quoted) and nine characters.
    CHECK(!loadText("P001 123456 \"\" \"M\" count 1.00 0 0 1 a.png 12 #\n").isEmpty());
    CHECK(!loadText("P001 123456 ABCDEFGHI \"M\" count 1.00 0 0 1 a.png 12 #\n").isEmpty());
    CHECK(loadText("P001 123456 ABCDEFGH \"M\" count 1.00 0 0 1 a.png 12 #\n").isEmpty());  // 8 is fine

    // Wrong number of fields.
    e = loadText("P001 123456 MILK \"M\" count 1.00 0 0 1 12 #\n");
    CHECK_CONTAINS(e, "expected 11 fields");
    e = loadText("P001 123456 MILK \"M\" count 1.00 0 0 1 a.png extra 12 #\n");
    CHECK_CONTAINS(e, "expected 11 fields");

    // Numbers.
    CHECK(!loadText("P001 123456 MILK \"M\" count 200.001 0 0 1 a.png 12 #\n").isEmpty());
    CHECK(!loadText("P001 123456 MILK \"M\" count -1.00 0 0 1 a.png 12 #\n").isEmpty());
    CHECK(!loadText("P001 123456 MILK \"M\" count abc 0 0 1 a.png 12 #\n").isEmpty());
    CHECK(!loadText("P001 123456 MILK \"M\" count 1.00 0 0 2.5 a.png 12 #\n").isEmpty());      // count stock must be whole
    CHECK(!loadText("P001 123456 MILK \"M\" weight 1.00 0 0 1.2345 a.png 12 #\n").isEmpty());  // 4 decimals
    CHECK(!loadText("P001 123456 MILK \"M\" count 1.00 0 5.00 1 a.png 12 #\n").isEmpty());     // discount > price+tax
    CHECK(!loadText("P001 123456 MILK \"M\" count 99999999999999999999 0 0 1 a.png 12 #\n").isEmpty());  // overflow
    CHECK(loadText("P001 123456 MILK \"M\" count 200 10 5 0 a.png 12 #\n").isEmpty());          // zero stock is valid

    // Quoting errors.
    e = loadText("P001 123456 MILK \"Whole Milk count 200.00 10.00 5.00 10 a.png 12 #\n");
    CHECK_CONTAINS(e, "line 1");
    CHECK(!loadText("P001 123456 MILK \"M\" count 1.00 0 0 1 \"a\\x.png\" 12 #\n").isEmpty());  // bad escape
    CHECK(!loadText("P001 123456 MI\"LK \"M\" count 1.00 0 0 1 a.png 12 #\n").isEmpty());       // stray quote

    // Empty file / only blank lines.
    CHECK(!loadText("").isEmpty());
    CHECK(!loadText("\n\n  \n").isEmpty());

    // Invalid UTF-8 byte.
    CHECK(!loadText(QByteArray("P001 123456 MILK \"M\xFF\" count 1.00 0 0 1 a.png 12 #\n")).isEmpty());

    // Unreadable file.
    ProductCatalog c;
    QString err;
    CHECK(!c.load(QStringLiteral("/definitely/not/here/Product.txt"), &err));
    CHECK(!err.isEmpty());
}

static void testQuotingAndFormats()
{
    std::printf("quoting, escapes, BOM, CRLF\n");
    ProductCatalog c;
    Product p;

    // Escaped quote and backslash inside a name; quoted path with spaces; leading zeros kept.
    QString e = loadText(
        "007 001234 \"AB C\" \"Ali \\\"Q\\\" \\\\ Co\" count 1.5 0 0 3 \"my images/001 234.png\" 00 #\n", &c);
    CHECK(e.isEmpty());
    CHECK(c.findByBarcode(QStringLiteral("001234"), p));
    CHECK(p.id == QStringLiteral("007"));
    CHECK(p.acronym == QStringLiteral("AB C"));
    CHECK(p.name == QStringLiteral("Ali \"Q\" \\ Co"));
    CHECK(p.barcodePath == QStringLiteral("my images/001 234.png"));
    CHECK(p.pricePaisa == 150);

    // UTF-8 BOM + CRLF line endings + blank line between rows.
    QByteArray crlf("\xEF\xBB\xBF");
    crlf += "P001 123456 MILK \"Whole Milk\" count 200.00 10.00 5.00 10 a.png 12 #\r\n\r\n";
    crlf += "P002 987654 RICE \"White Rice\" weight 300.00 20.00 10.00 5.000 b.png 98 #\r\n";
    ProductCatalog c2;
    CHECK(loadText(crlf, &c2).isEmpty());
    CHECK(c2.size() == 2);

    // Last line without a trailing newline still loads.
    ProductCatalog c3;
    CHECK(loadText(QByteArray("P001 123456 MILK \"M\" count 1 0 0 1 a.png 12 #"), &c3).isEmpty());

    // Non-ASCII name.
    ProductCatalog c4;
    CHECK(loadText(QString::fromUtf8("P001 123456 DOODH \"Doodh \xE2\x80\x93 \xD8\xAF\xD9\x88\xD8\xAF\xDA\xBE\" count 1 0 0 1 a.png 12 #\n").toUtf8(), &c4).isEmpty());
    CHECK(c4.findByBarcode(QStringLiteral("123456"), p));
    CHECK(p.name.contains(QChar(0x062F)));
}

static void testNumberHelpers()
{
    std::printf("number helpers\n");
    qint64 v = 0;
    CHECK(parsePaisa(QStringLiteral("200"), v) && v == 20000);
    CHECK(parsePaisa(QStringLiteral("200.5"), v) && v == 20050);
    CHECK(parsePaisa(QStringLiteral("0.05"), v) && v == 5);
    CHECK(!parsePaisa(QStringLiteral(".5"), v));
    CHECK(!parsePaisa(QStringLiteral("5."), v));
    CHECK(!parsePaisa(QStringLiteral("+5"), v));
    CHECK(!parsePaisa(QStringLiteral("1e3"), v));
    CHECK(!parsePaisa(QString(), v));

    CHECK(parseQuantityMilli(QStringLiteral("2"), Unit::Count, v) && v == 2000);
    CHECK(!parseQuantityMilli(QStringLiteral("0.500"), Unit::Count, v));
    CHECK(parseQuantityMilli(QStringLiteral("0.500"), Unit::Weight, v) && v == 500);
    CHECK(parseQuantityMilli(QStringLiteral("5"), Unit::Weight, v) && v == 5000);
    CHECK(parseQuantityMilli(QStringLiteral("0.001"), Unit::Weight, v) && v == 1);
    CHECK(!parseQuantityMilli(QStringLiteral("0.0005"), Unit::Weight, v));

    CHECK(formatPaisa(20000) == QStringLiteral("200.00"));
    CHECK(formatPaisa(56500) == QStringLiteral("565.00"));
    CHECK(formatPaisa(5) == QStringLiteral("0.05"));
    CHECK(formatPaisa(0) == QStringLiteral("0.00"));
    CHECK(formatPaisa(-250) == QStringLiteral("-2.50"));
    CHECK(formatQuantityMilli(2000, Unit::Count) == QStringLiteral("2"));
    CHECK(formatQuantityMilli(10000, Unit::Count) == QStringLiteral("10"));
    CHECK(formatQuantityMilli(500, Unit::Weight) == QStringLiteral("0.500"));
    CHECK(formatQuantityMilli(5000, Unit::Weight) == QStringLiteral("5.000"));
    CHECK(formatQuantityMilli(4500, Unit::Weight) == QStringLiteral("4.500"));

    CHECK(isValidBarcodeText(QStringLiteral("000001")));
    CHECK(!isValidBarcodeText(QStringLiteral("00001")));
    CHECK(!isValidBarcodeText(QStringLiteral("0000a1")));
    CHECK(!isValidBarcodeText(QString::fromUtf8("\xD9\xA1\xD9\xA2\xD9\xA3\xD9\xA4\xD9\xA5\xD9\xA6")));  // Arabic-Indic digits
}

static void testPublishIsAtomic()
{
    std::printf("failed reload keeps the old catalog\n");
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("Product.txt"));
    writeFile(path, kGoodRows);

    ProductCatalog catalog;
    QString error;
    CHECK(catalog.load(path, &error));

    // Second file: first row fine, second row broken -> nothing may be swapped in.
    writeFile(path,
              "P009 555555 TEA \"Tea\" count 1.00 0 0 1 t.png 55 #\n"
              "P010 666666 BAD \"Bad\" count oops 0 0 1 b.png 66 #\n");
    CHECK(!catalog.load(path, &error));
    CHECK_CONTAINS(error, "line 2");

    Product p;
    CHECK(catalog.size() == 2);
    CHECK(catalog.findByBarcode(QStringLiteral("123456"), p));
    CHECK(!catalog.findByBarcode(QStringLiteral("555555"), p));   // partial data never published

    // A good reload replaces the old data completely.
    writeFile(path, "P009 555555 TEA \"Tea\" count 1.00 0 0 1 t.png 55 #\n");
    CHECK(catalog.load(path, &error));
    CHECK(catalog.size() == 1);
    CHECK(catalog.findByBarcode(QStringLiteral("555555"), p));
    CHECK(!catalog.findByBarcode(QStringLiteral("123456"), p));
}

static void testPathResolution()
{
    std::printf("relative path resolves from Product.txt, not the working directory\n");
    QTemporaryDir dataDir;
    QTemporaryDir otherDir;
    QDir().mkpath(dataDir.filePath(QStringLiteral("barcodes")));
    writeFile(dataDir.filePath(QStringLiteral("barcodes/123456.png")), "png");
    writeFile(dataDir.filePath(QStringLiteral("Product.txt")), kGoodRows);

    const QString originalCwd = QDir::currentPath();

    // Load with a RELATIVE path, then move the process elsewhere.
    QDir::setCurrent(dataDir.path());
    ProductCatalog catalog;
    QString error;
    CHECK(catalog.load(QStringLiteral("Product.txt"), &error));
    QDir::setCurrent(otherDir.path());

    Product p;
    CHECK(catalog.findByBarcode(QStringLiteral("123456"), p));
    const QString resolved = catalog.resolveBarcodePath(p);
    CHECK(QFileInfo(resolved).isAbsolute());
    CHECK(QFileInfo::exists(resolved));
    CHECK(QFileInfo(resolved).canonicalFilePath() ==
          QFileInfo(dataDir.filePath(QStringLiteral("barcodes/123456.png"))).canonicalFilePath());

    // Absolute paths inside Product.txt are respected.
    Product abs = p;
    abs.barcodePath = dataDir.filePath(QStringLiteral("barcodes/123456.png"));
    CHECK(QFileInfo::exists(catalog.resolveBarcodePath(abs)));
    Product none = p;
    none.barcodePath.clear();
    CHECK(catalog.resolveBarcodePath(none).isEmpty());

    QDir::setCurrent(originalCwd);
}

static void testStockAndSerialization()
{
    std::printf("stock deduction + serialization keep the other nine fields\n");
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("Product.txt"));
    writeFile(path, kGoodRows);

    ProductCatalog original;
    QString error;
    CHECK(original.load(path, &error));

    // Round trip with no change is byte-identical to the layout in the guide.
    CHECK(original.serialize() == QByteArray(kGoodRows));

    // Deduct on a COPY (what Shahzeen does), the original is untouched.
    ProductCatalog copy = original;
    CHECK(copy.deductStock(QStringLiteral("P001"), 2000, &error));      // milk 10 -> 8
    CHECK(copy.deductStock(QStringLiteral("P002"), 500, &error));       // rice 5.000 -> 4.500
    Product p;
    CHECK(original.findByBarcode(QStringLiteral("123456"), p) && p.stockMilli == 10000);

    const QByteArray expected =
        "P001 123456 MILK \"Whole Milk\" count 200.00 10.00 5.00 8 barcodes/123456.png 12 #\n"
        "P002 987654 RICE \"White Rice\" weight 300.00 20.00 10.00 4.500 barcodes/987654.png 98 #\n";
    CHECK(copy.serialize() == expected);

    // save() then reload.
    CHECK(copy.save(path, &error));
    ProductCatalog reloaded;
    CHECK(reloaded.load(path, &error));
    CHECK(reloaded.findByBarcode(QStringLiteral("123456"), p));
    CHECK(p.stockMilli == 8000 && p.name == QStringLiteral("Whole Milk") &&
          p.pricePaisa == 20000 && p.taxPaisa == 1000 && p.discountPaisa == 500 &&
          p.barcodePath == QStringLiteral("barcodes/123456.png") && p.acronym == QStringLiteral("MILK"));
    CHECK(reloaded.findByBarcode(QStringLiteral("987654"), p));
    CHECK(p.stockMilli == 4500 && p.unit == Unit::Weight);

    // Deduction rules.
    CHECK(!copy.deductStock(QStringLiteral("P001"), 0, &error));
    CHECK(!copy.deductStock(QStringLiteral("P001"), -5, &error));
    CHECK(!copy.deductStock(QStringLiteral("P001"), 9000, &error));    // more than the 8 left
    CHECK_CONTAINS(error, "not enough stock");
    CHECK(!copy.deductStock(QStringLiteral("NOPE"), 1000, &error));
    CHECK(copy.deductStock(QStringLiteral("P001"), 8000, &error));      // down to exactly 0 is allowed
    ProductCatalog zeroed;
    QString zeroErr = loadText(copy.serialize(), &zeroed);
    CHECK(zeroErr.isEmpty());                                           // a 0-stock catalog reloads

    // Names that need escaping survive the round trip.
    ProductCatalog tricky;
    CHECK(loadText("007 001234 \"AB C\" \"Ali \\\"Q\\\" \\\\ Co\" count 1.5 0 0 3 \"my images/x.png\" 00 #\n", &tricky).isEmpty());
    ProductCatalog again;
    CHECK(loadText(tricky.serialize(), &again).isEmpty());
    CHECK(again.serialize() == tricky.serialize());
    CHECK(again.findByBarcode(QStringLiteral("001234"), p) && p.name == QStringLiteral("Ali \"Q\" \\ Co"));

    // Unwritable target is reported, not ignored.
    CHECK(!copy.save(QStringLiteral("/definitely/not/here/Product.txt"), &error));
    CHECK(!error.isEmpty());
}

static void testCategoryIdRules()
{
    std::printf("category id is stored and must match the barcode\n");
    QString e;

    e = loadText("P001 123456 MILK \"M\" count 1.00 0 0 1 a.png 13 #\n");     // 13 != 12
    CHECK_CONTAINS(e, "line 1");
    CHECK_CONTAINS(e, "category ID");
    CHECK(!loadText("P001 123456 MILK \"M\" count 1.00 0 0 1 a.png 1 #\n").isEmpty());    // one digit
    CHECK(!loadText("P001 123456 MILK \"M\" count 1.00 0 0 1 a.png 1A #\n").isEmpty());   // letter

    ProductCatalog c;
    Product p;
    CHECK(loadText(kGoodRows, &c).isEmpty());
    CHECK(c.findByBarcode(QStringLiteral("987654"), p) && p.categoryId == QStringLiteral("98"));
}

static void testManagerFeatures()
{
    std::printf("barcode generation, add, update, set stock\n");
    ProductCatalog c;
    CHECK(loadText(kGoodRows, &c).isEmpty());
    QString error;

    // nextBarcode: category id + 4-digit number, skips used numbers.
    CHECK(c.nextBarcode(QStringLiteral("12")) == QStringLiteral("120001"));
    CHECK(c.nextBarcode(QStringLiteral("77")) == QStringLiteral("770001"));
    CHECK(c.nextBarcode(QStringLiteral("1")).isEmpty());
    CHECK(c.nextBarcode(QStringLiteral("1A")).isEmpty());

    // addProduct: valid product is appended at the end.
    Product tea;
    tea.id = QStringLiteral("P003");
    tea.categoryId = QStringLiteral("12");
    tea.barcode = c.nextBarcode(tea.categoryId);
    tea.acronym = QStringLiteral("TEA");
    tea.name = QStringLiteral("Black Tea");
    tea.unit = Unit::Count;
    tea.pricePaisa = 45000;
    tea.stockMilli = 4000;
    tea.barcodePath = QStringLiteral("barcodes/") + tea.barcode + QStringLiteral(".png");
    CHECK(c.addProduct(tea, &error));
    CHECK(c.size() == 3);
    CHECK(c.products().last().id == QStringLiteral("P003"));
    CHECK(c.nextBarcode(QStringLiteral("12")) == QStringLiteral("120002"));
    CHECK_CONTAINS(QString::fromUtf8(c.serialize()), "P003 120001 TEA \"Black Tea\" count 450.00 0.00 0.00 4 barcodes/120001.png 12 #");

    // addProduct rules.
    Product bad = tea;
    CHECK(!c.addProduct(bad, &error));                         // same ID and barcode
    bad.id = QStringLiteral("P004");
    CHECK(!c.addProduct(bad, &error));                         // barcode already used
    CHECK_CONTAINS(error, "already used");
    bad.barcode = QStringLiteral("130001");
    CHECK(!c.addProduct(bad, &error));                         // category 12 != barcode prefix 13
    CHECK_CONTAINS(error, "category");
    bad.categoryId = QStringLiteral("13");
    bad.acronym.clear();
    CHECK(!c.addProduct(bad, &error));                         // empty acronym
    bad.acronym = QStringLiteral("TEA2");
    bad.discountPaisa = 999999;
    CHECK(!c.addProduct(bad, &error));                         // discount > price + tax
    bad.discountPaisa = 0;
    bad.stockMilli = 1500;
    CHECK(!c.addProduct(bad, &error));                         // count with a fraction
    CHECK(c.size() == 3);                                      // nothing half-added

    // updateProduct: other fields change, barcode/category cannot.
    Product edit;
    CHECK(c.findById(QStringLiteral("P001"), edit));
    edit.name = QStringLiteral("Fresh Milk");
    edit.pricePaisa = 21000;
    CHECK(c.updateProduct(edit, &error));
    Product check;
    CHECK(c.findByBarcode(QStringLiteral("123456"), check) && check.name == QStringLiteral("Fresh Milk") && check.pricePaisa == 21000);
    CHECK(c.products().first().id == QStringLiteral("P001"));  // order unchanged

    Product moved = edit;
    moved.barcode = QStringLiteral("123457");
    CHECK(!c.updateProduct(moved, &error));
    moved = edit;
    moved.categoryId = QStringLiteral("99");
    CHECK(!c.updateProduct(moved, &error));
    moved = edit;
    moved.id = QStringLiteral("NOPE");
    CHECK(!c.updateProduct(moved, &error));

    // setStock: replaces the quantity; zero allowed; negatives / fractions of a count refused.
    CHECK(c.setStock(QStringLiteral("P002"), 12500, &error));
    CHECK(c.findByBarcode(QStringLiteral("987654"), check) && check.stockMilli == 12500);
    CHECK(c.setStock(QStringLiteral("P001"), 0, &error));
    CHECK(!c.setStock(QStringLiteral("P001"), -1, &error));
    CHECK(!c.setStock(QStringLiteral("P001"), 1500, &error));   // milk is counted
    CHECK(!c.setStock(QStringLiteral("NOPE"), 1000, &error));

    // filePath() and save() round trip.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("Product.txt"));
    writeFile(path, kGoodRows);
    ProductCatalog saved;
    CHECK(saved.load(path, &error));
    CHECK(QFileInfo(saved.filePath()).canonicalFilePath() == QFileInfo(path).canonicalFilePath());
    CHECK(saved.addProduct(tea, &error));
    CHECK(saved.save(saved.filePath(), &error));
    ProductCatalog again;
    CHECK(again.load(path, &error) && again.size() == 3);
}

static void testCategoryCatalog()
{
    std::printf("PCategory.txt\n");
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("PCategory.txt"));
    QString error;

    // Missing file = no categories yet (not an error).
    CategoryCatalog cats;
    CHECK(cats.load(path, &error));
    CHECK(cats.categories().isEmpty());
    CHECK(cats.nextFreeId() == QStringLiteral("01"));

    // Add, reject duplicates and bad values.
    CHECK(cats.add({QStringLiteral("12"), QStringLiteral("Dairy Products"), QStringLiteral("DRY")}, &error));
    CHECK(cats.add({QStringLiteral("01"), QStringLiteral("Grocery"), QStringLiteral("GRO")}, &error));
    CHECK(!cats.add({QStringLiteral("12"), QStringLiteral("Other"), QStringLiteral("OTH")}, &error));
    CHECK_CONTAINS(error, "already used");
    CHECK(!cats.add({QStringLiteral("1"), QStringLiteral("X"), QStringLiteral("X")}, &error));
    CHECK(!cats.add({QStringLiteral("1A"), QStringLiteral("X"), QStringLiteral("X")}, &error));
    CHECK(!cats.add({QStringLiteral("123"), QStringLiteral("X"), QStringLiteral("X")}, &error));
    CHECK(!cats.add({QStringLiteral("20"), QString(), QStringLiteral("X")}, &error));
    CHECK(!cats.add({QStringLiteral("20"), QStringLiteral("X"), QString()}, &error));
    CHECK(!cats.add({QStringLiteral("20"), QStringLiteral("X"), QStringLiteral("TOOLONGACR")}, &error));
    CHECK(cats.nextFreeId() == QStringLiteral("02"));

    // Save, check the file text, reload.
    CHECK(cats.save(&error));
    QFile f(path);
    CHECK(f.open(QIODevice::ReadOnly));
    CHECK(f.readAll() == QByteArray("12 \"Dairy Products\" DRY #\n01 \"Grocery\" GRO #\n"));
    f.close();

    CategoryCatalog reloaded;
    CHECK(reloaded.load(path, &error));
    CHECK(reloaded.categories().size() == 2);
    Category c;
    CHECK(reloaded.findById(QStringLiteral("12"), c) && c.name == QStringLiteral("Dairy Products") && c.acronym == QStringLiteral("DRY"));
    CHECK(!reloaded.findById(QStringLiteral("99"), c));

    // Bad files report the line and keep the old data.
    writeFile(path, "12 Dairy DRY #\n13 Bakery\n");
    CHECK(!reloaded.load(path, &error));
    CHECK_CONTAINS(error, "line 2");
    CHECK(reloaded.categories().size() == 2);
    writeFile(path, "12 Dairy DRY #\n12 Bakery BAK #\n");
    CHECK(!reloaded.load(path, &error));
    CHECK_CONTAINS(error, "line 2");
    writeFile(path, "12 Dairy DRY\n");
    CHECK(!reloaded.load(path, &error));
    CHECK_CONTAINS(error, "#");
    writeFile(path, "12 Dairy #\n");
    CHECK(!reloaded.load(path, &error));
    CHECK_CONTAINS(error, "expected 3 fields");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString dataDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("data");

    testSampleFile(dataDir);
    testRejections();
    testQuotingAndFormats();
    testNumberHelpers();
    testPublishIsAtomic();
    testPathResolution();
    testStockAndSerialization();
    testCategoryIdRules();
    testManagerFeatures();
    testCategoryCatalog();

    std::printf("\n%d checks passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
