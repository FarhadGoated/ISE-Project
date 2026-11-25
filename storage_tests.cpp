// Runs the page 9 "checks to run" plus the worked checkout from page 10.
#include "../CheckoutStorage.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

using namespace store;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { ++g_fail; std::cerr << "FAIL line " << __LINE__ << ": " #c "\n"; } } while (0)

// Stand-in for Anaya's ProductCatalog.
struct FakeInventory : IInventory {
    QHash<QString, qint64> stock;
    bool failApply = false;
    QHash<QString, qint64> physicalStock() override { return stock; }
    bool applyStock(const QHash<QString, qint64> &s, QString *err) override {
        if (failApply) { *err = "inventory unwritable"; return false; }
        for (auto it = s.cbegin(); it != s.cend(); ++it) stock[it.key()] = it.value();
        return true;
    }
};

static OrderRecord makeOrder(const QString &id, qint64 qtyMilli, const QString &pid = "P001")
{
    OrderRecord o;
    o.orderId = id;
    o.customerName = "Ali Khan";
    o.paymentMode = "cash";
    OrderLine l;
    l.productId = pid; l.barcode = "123456"; l.qtyMilli = qtyMilli; l.acronym = "MILK";
    l.unitPricePaisa = 20000; l.taxPerUnitPaisa = 1000; l.discountPerUnitPaisa = 500;
    o.lines << l;
    return o;
}

static BillRecord workedBill(const QString &orderId)
{
    BillRecord b;
    b.orderId = orderId; b.customerName = "Ali Khan";
    b.totalBeforeTax = 40000; b.totalTax = 2000; b.totalProductDiscount = 1000;
    b.seasonalDiscount = 3900; b.specialDiscount = 1755; b.totalPayable = 35345;
    b.amountReceived = 50000; b.balanceReturned = 14655;
    b.billDate = QDate(2026, 12, 1); b.billTime = QTime(10, 30, 0);
    BillLine l; l.acronym = "MILK"; l.unitPricePaisa = 20000; l.qtyMilli = 2000; l.productDiscountPaisa = 1000;
    b.lines << l;
    return b;
}

static QString slurp(const QString &p) { QFile f(p); f.open(QIODevice::ReadOnly); return QString::fromUtf8(f.readAll()); }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QDateTime t0(QDate(2026, 12, 1), QTime(10, 30, 0));

    // ---- worked checkout, file format, restart ----
    {
        QTemporaryDir dir; FakeInventory inv; inv.stock["P001"] = 10000;
        CheckoutStorage s(dir.path(), &inv); CHECK(s.load());
        QString id;
        Result r = s.commitPaidSale(makeOrder("00000101", 2000), workedBill("00000101"), t0, &id);
        CHECK(r.ok); CHECK(id == "000000001"); CHECK(inv.stock["P001"] == 8000);
        CHECK(slurp(dir.filePath("Bill.txt")) ==
              "000000001 \"Ali Khan\" 00000101 400.00 20.00 10.00 353.45 500.00 146.55 paid 39.00 17.55 2026-12-01 10:30:00 #\n");
        CHECK(slurp(dir.filePath("BillDetail.txt")) == "000000001 MILK 200.00 2 10.00 #\n");
        CHECK(slurp(dir.filePath("OrderDetail.txt")) == "00000101 P001 123456 2\n");
        // paying twice is refused
        CHECK(!s.commitPaidSale(makeOrder("00000101", 2000), workedBill("00000101"), t0).ok);

        CheckoutStorage s2(dir.path(), &inv); CHECK(s2.load());
        BillRecord b; CHECK(s2.activeBillForOrder("00000101", &b)); CHECK(b.totalPayable == 35345);
        CHECK(b.lines.size() == 1 && b.lines[0].qtyMilli == 2000);

        // ---- paid revision: 2 -> 3 pieces ----
        BillRecord nb = workedBill("00000101"); nb.lines[0].qtyMilli = 3000;
        QString id2;
        r = s2.commitPaidRevision(makeOrder("00000101", 3000), nb, t0.addSecs(3600), &id2);
        CHECK(r.ok); CHECK(id2 == "000000002"); CHECK(inv.stock["P001"] == 7000);
        CHECK(s2.activeBills().size() == 1);
        CHECK(s2.billHistory("00000101").size() == 2);
        BillRecord old; CHECK(s2.bill("000000001", &old)); CHECK(old.superseded && old.supersededBy == "000000002");
        OrderRecord po; s2.order("00000101", &po); CHECK(po.paidAt == t0); // original timestamp kept
        CHECK(s2.soldQuantities().value("P001") == 3000);
        // window: 169 hours after original payment
        CHECK(!s2.checkPaidEditWindow("00000101", t0.addSecs(169 * 3600)).ok);
        CHECK(!s2.commitPaidRevision(makeOrder("00000101", 1000), nb, t0.addSecs(169 * 3600)).ok);
        // archive
        SweepResult sw = s2.sweep(t0.addSecs(168 * 3600));
        CHECK(sw.archivedOrders == QStringList{"00000101"});
        BillRecord ab; s2.activeBillForOrder("00000101", &ab); CHECK(ab.paymentStatus == "archived");
        CHECK(s2.activeBills().size() == 1 && s2.completedOrders().size() == 1);
    }

    // ---- holds survive restart, expire, and reserve stock ----
    {
        QTemporaryDir dir; FakeInventory inv; inv.stock["P001"] = 10000;
        CheckoutStorage s(dir.path(), &inv); s.load();
        CHECK(s.holdOrder(makeOrder("A1", 4000), t0).ok);
        CHECK(s.holdOrder(makeOrder("A2", 3000), t0.addSecs(60)).ok);
        CHECK(!s.holdOrder(makeOrder("A3", 4000), t0).ok);       // only 3 available
        CheckoutStorage r(dir.path(), &inv); CHECK(r.load());
        CHECK(r.reservedStock("P001", t0.addSecs(120)) == 7000);
        CHECK(r.availableStock("P001", t0.addSecs(120)) == 3000);
        CHECK(r.availableStock("P001", t0.addSecs(120), "A1") == 7000); // own entitlement on resume
        OrderRecord held; CHECK(r.checkResume("A1", t0.addSecs(600), &held).ok);
        CHECK(held.holdExpiry == t0.addSecs(30 * 60));                   // timer keeps running
        // sell the resumed order
        BillRecord b = workedBill("A1"); b.lines[0].qtyMilli = 4000;
        CHECK(r.commitPaidSale(held, b, t0.addSecs(900)).ok);
        CHECK(inv.stock["P001"] == 6000 && r.reservedStock("P001", t0.addSecs(900)) == 3000);
        // cancellation releases
        CHECK(r.cancelOrder("A2").ok); CHECK(r.reservedStock("P001", t0.addSecs(900)) == 0);
        // expiry
        CHECK(r.holdOrder(makeOrder("A4", 1000), t0).ok);
        SweepResult sw = r.sweep(t0.addSecs(31 * 60));
        CHECK(sw.expiredOrders == QStringList{"A4"});
        CHECK(!r.checkResume("A4", t0.addSecs(32 * 60), nullptr).ok);
    }

    // ---- failed saves ----
    {
        QTemporaryDir dir; FakeInventory inv; inv.stock["P001"] = 10000;
        CheckoutStorage s(dir.path(), &inv); s.load();
        s.setFaultInjection("Bill.txt");
        Result r = s.commitPaidSale(makeOrder("B1", 2000), workedBill("B1"), t0);
        CHECK(!r.ok); CHECK(!r.recoveryFailed); CHECK(inv.stock["P001"] == 10000);
        CHECK(!QFile::exists(dir.filePath("Order.txt")));        // nothing half written
        s.setFaultInjection("");
        CHECK(s.commitPaidSale(makeOrder("B1", 2000), workedBill("B1"), t0).ok); // retry works

        // swap fails midway, recovery works
        inv.failApply = true;
        CHECK(!s.commitPaidSale(makeOrder("B2", 1000), workedBill("B2"), t0).ok);
        inv.failApply = false; CHECK(inv.stock["P001"] == 8000);

        // swap fails and recovery fails: locked, no second submit
        s.setFaultInjection("Bill.txt", true);
        r = s.commitPaidSale(makeOrder("B3", 1000), workedBill("B3"), t0);
        CHECK(!r.ok && r.recoveryFailed && s.isLocked());
        s.setFaultInjection("");
        CHECK(!s.commitPaidSale(makeOrder("B3", 1000), workedBill("B3"), t0).ok);
    }

    // ---- malformed input gives useful errors ----
    {
        QTemporaryDir dir; FakeInventory inv;
        QFile f(dir.filePath("Order.txt")); f.open(QIODevice::WriteOnly);
        f.write("00000101 2026-12-01 10:30:00 \"Ali Khan\" \"\" paid 12x.00 cash \"\" #\nshort row #\n");
        f.close();
        CheckoutStorage s(dir.path(), &inv);
        CHECK(!s.load());
        CHECK(s.loadErrors().size() == 2);
        for (const QString &e : s.loadErrors()) std::cout << "  load error: " << e.toStdString() << "\n";
    }

    std::cout << (g_fail ? "SOME TESTS FAILED\n" : "ALL TESTS PASSED\n");
    return g_fail ? 1 : 0;
}
