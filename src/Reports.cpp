#include "Reports.h"

#include "BillCalculator.h"
#include "CheckoutStorage.h"
#include "ProductCatalog.h"
#include "SeasonDiscounts.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLockFile>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace shop {
namespace {

enum Report { OpenOrders, CompletedOrders, CashSales, AllSales, MostSold, HighestStock };
enum Period { Daily, Weekly, Monthly, Yearly, Custom };

struct DateRange {
    QDate start;
    QDate end;
};

DateRange dateRange(Period period, const QDate& today, const QDate& start, const QDate& end)
{
    switch (period) {
    case Daily:
        return {today, today};
    case Weekly: {
        const QDate monday = today.addDays(1 - today.dayOfWeek());
        return {monday, monday.addDays(6)};
    }
    case Monthly:
        return {QDate(today.year(), today.month(), 1),
                QDate(today.year(), today.month(), today.daysInMonth())};
    case Yearly:
        return {QDate(today.year(), 1, 1), QDate(today.year(), 12, 31)};
    case Custom:
        return {start, end};
    }
    return {today, today};
}

QString money(qint64 paisa)
{
    return QStringLiteral("PKR %1").arg(store::formatPkr(paisa));
}

bool addAmount(qint64& total, qint64 amount)
{
    if (amount < 0 || amount > std::numeric_limits<qint64>::max() - total)
        return false;
    total += amount;
    return true;
}

QString displayTotal(bool valid, qint64 paisa)
{
    return valid ? money(paisa) : QStringLiteral("Unavailable");
}

bool estimateOrder(const store::OrderRecord& saved, int seasonalRate, qint64& payable)
{
    // Estimates use the stored price snapshots. Current stock is checked at checkout.
    Order order([](const Product&) { return std::numeric_limits<qint64>::max(); });
    for (const store::OrderLine& line : saved.lines) {
        if (line.acronym.isEmpty())
            return false;
        Product product;
        product.id = line.productId;
        product.acronym = line.acronym;
        product.name = line.acronym;
        product.barcode = line.barcode;
        product.unit = line.weighted ? Unit::Weight : Unit::Count;
        product.pricePaisa = line.unitPricePaisa;
        product.taxPaisa = line.taxPerUnitPaisa;
        product.discountPaisa = line.discountPerUnitPaisa;
        if (order.add(product, line.qtyMilli, nullptr, AtLimitChoice::SellAll) != AddOutcome::Added)
            return false;
    }

    DiscountRates rates;
    rates.seasonalBasisPoints = seasonalRate;
    Bill bill;
    if (!BillCalculator::calculate(order, rates, bill))
        return false;
    payable = bill.totals.payablePaisa;
    return true;
}

class ReportsPage final : public QWidget {
public:
    ReportsPage(const QString& dataDir, QWidget* parent)
        : QWidget(parent), dataDir_(dataDir)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(12);
        auto* title = new QLabel(QStringLiteral("Reports"), this);
        title->setObjectName(QStringLiteral("pageHeading"));
        layout->addWidget(title);

        auto* toolbar = new QHBoxLayout;
        report_ = new QComboBox(this);
        report_->setAccessibleName(QStringLiteral("Report"));
        report_->addItems({QStringLiteral("Open Orders"), QStringLiteral("Completed Orders"),
            QStringLiteral("Cash Sales"), QStringLiteral("All Sales"),
            QStringLiteral("Top 10 Most Sold"), QStringLiteral("Top 10 Stock on Hand")});
        period_ = new QComboBox(this);
        period_->setAccessibleName(QStringLiteral("Cash sales period"));
        period_->addItems({QStringLiteral("Daily"), QStringLiteral("Weekly"),
            QStringLiteral("Monthly"), QStringLiteral("Yearly"), QStringLiteral("Custom Dates")});
        units_ = new QComboBox(this);
        units_->setAccessibleName(QStringLiteral("Stock unit"));
        units_->addItems({QStringLiteral("Count"), QStringLiteral("Weight")});
        auto* refresh = new QPushButton(QStringLiteral("Refresh"), this);
        refresh->setAutoDefault(false);
        toolbar->addWidget(report_, 1);
        toolbar->addWidget(period_);
        toolbar->addWidget(units_);
        toolbar->addWidget(refresh);
        layout->addLayout(toolbar);

        dates_ = new QWidget(this);
        auto* datesLayout = new QHBoxLayout(dates_);
        datesLayout->setContentsMargins(0, 0, 0, 0);
        from_ = new QDateEdit(QDate::currentDate(), dates_);
        to_ = new QDateEdit(QDate::currentDate(), dates_);
        for (auto* date : {from_, to_}) {
            date->setCalendarPopup(true);
            date->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
        }
        auto* fromLabel = new QLabel(QStringLiteral("From"), dates_);
        auto* toLabel = new QLabel(QStringLiteral("To"), dates_);
        fromLabel->setBuddy(from_);
        toLabel->setBuddy(to_);
        datesLayout->addWidget(fromLabel);
        datesLayout->addWidget(from_);
        datesLayout->addWidget(toLabel);
        datesLayout->addWidget(to_);
        datesLayout->addStretch();
        layout->addWidget(dates_);

        table_ = new QTableWidget(this);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setAlternatingRowColors(true);
        table_->verticalHeader()->hide();
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setStretchLastSection(true);
        layout->addWidget(table_, 1);

        summary_ = new QLabel(this);
        summary_->setTextFormat(Qt::PlainText);
        summary_->setWordWrap(true);
        layout->addWidget(summary_);

        connect(report_, &QComboBox::currentIndexChanged, this, [this] { refreshReport(); });
        connect(period_, &QComboBox::currentIndexChanged, this, [this] { refreshReport(); });
        connect(units_, &QComboBox::currentIndexChanged, this, [this] { refreshReport(); });
        connect(from_, &QDateEdit::dateChanged, this, [this] { refreshReport(); });
        connect(to_, &QDateEdit::dateChanged, this, [this] { refreshReport(); });
        connect(refresh, &QPushButton::clicked, this, [this] { refreshReport(); });
        refreshReport();
    }

protected:
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        refreshReport();
    }

private:
    void columns(const QStringList& headers)
    {
        table_->setRowCount(0);
        table_->setColumnCount(headers.size());
        table_->setHorizontalHeaderLabels(headers);
    }

    void row(const QStringList& cells)
    {
        const int index = table_->rowCount();
        table_->insertRow(index);
        for (int column = 0; column < cells.size(); ++column)
            table_->setItem(index, column, new QTableWidgetItem(cells.at(column)));
    }

    void refreshReport()
    {
        const auto report = static_cast<Report>(report_->currentIndex());
        const auto period = static_cast<Period>(period_->currentIndex());
        const bool ranking = report == MostSold || report == HighestStock;
        period_->setVisible(report == CashSales);
        dates_->setVisible(report == CashSales && period == Custom);
        units_->setVisible(ranking);
        columns({});
        summary_->clear();

        const QDir dir(dataDir_);
        if (dataDir_.isEmpty() || !dir.exists()) {
            summary_->setText(QStringLiteral("The data folder is unavailable."));
            return;
        }

        QLockFile lock(dir.filePath(QStringLiteral(".checkout.lock")));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0)) {
            summary_->setText(QStringLiteral("Checkout is updating the data. Refresh in a moment."));
            return;
        }

        store::CheckoutStorage storage(dataDir_, nullptr);
        if (report != HighestStock && !storage.load()) {
            summary_->setText(storage.loadErrors().join(QLatin1Char('\n')));
            return;
        }

        if (report == OpenOrders)
            showOpen(storage);
        else if (ranking)
            showRanking(storage, report == MostSold, dir);
        else
            showCompleted(storage, report, period);
    }

    void showOpen(const store::CheckoutStorage& storage)
    {
        columns({QStringLiteral("Order ID"), QStringLiteral("Customer"), QStringLiteral("Status"),
                 QStringLiteral("Hold expires"), QStringLiteral("Estimated payable")});
        QVector<SeasonDiscount> seasons;
        QString error;
        if (!loadSeasons(dataDir_, seasons, error)) {
            summary_->setText(error);
            return;
        }
        const QDateTime now = QDateTime::currentDateTime();
        const int seasonalRate = highestSeasonRate(seasons, now.date());
        qint64 total = 0;
        bool complete = true;
        for (const QString& id : storage.orderIds()) {
            store::OrderRecord order;
            if (!storage.order(id, &order))
                continue;
            const bool held = order.status == store::Status::OnHold;
            if (!held && order.status != store::Status::InProgress)
                continue;
            if (order.holdExpiry.isValid() && order.holdExpiry <= now)
                continue;
            qint64 estimate = 0;
            const bool estimated = estimateOrder(order, seasonalRate, estimate);
            complete = (estimated && addAmount(total, estimate)) && complete;
            row({id, order.customerName, held ? QStringLiteral("On hold") : QStringLiteral("In progress"),
                 order.holdExpiry.isValid() ? order.holdExpiry.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                            : QStringLiteral("-"),
                 displayTotal(estimated, estimate)});
        }
        summary_->setText(QStringLiteral("Open orders  %1    Estimated total  %2")
            .arg(table_->rowCount()).arg(displayTotal(complete, total)));
    }

    void showCompleted(const store::CheckoutStorage& storage, Report report, Period period)
    {
        auto orders = storage.completedOrders();
        std::sort(orders.begin(), orders.end(), [](const store::ReportOrder& first, const store::ReportOrder& second) {
            if (first.paidAt != second.paidAt)
                return first.paidAt > second.paidAt;
            return first.orderId < second.orderId;
        });

        const bool cash = report == CashSales;
        const bool sales = report == AllSales;
        const DateRange range = dateRange(period, QDate::currentDate(), from_->date(), to_->date());
        QStringList headers{QStringLiteral("Order ID"), QStringLiteral("Bill ID"), QStringLiteral("Customer"),
                            QStringLiteral("Original payment"), QStringLiteral("Status"), QStringLiteral("Payment")};
        if (sales) {
            headers << QStringLiteral("Product discount") << QStringLiteral("Seasonal discount")
                    << QStringLiteral("Special discount");
        }
        headers << QStringLiteral("Payable");
        columns(headers);
        if (cash && range.end < range.start) {
            summary_->setText(QStringLiteral("Choose an end date on or after the start date."));
            return;
        }

        qint64 total = 0;
        qint64 discounts = 0;
        bool totalValid = true;
        bool discountsValid = true;
        for (const store::ReportOrder& order : orders) {
            const QDate paidDate = order.paidAt.date();
            if (cash && (order.paymentMode.compare(QStringLiteral("cash"), Qt::CaseInsensitive) != 0
                || !paidDate.isValid() || paidDate < range.start || paidDate > range.end))
                continue;

            const store::BillRecord& bill = order.activeBill;
            totalValid = addAmount(total, bill.totalPayable) && totalValid;
            QStringList cells{order.orderId, bill.billId, bill.customerName,
                              order.paidAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                              order.status, order.paymentMode};
            if (sales) {
                cells << money(bill.totalProductDiscount) << money(bill.seasonalDiscount) << money(bill.specialDiscount);
                discountsValid = addAmount(discounts, bill.totalProductDiscount)
                    && addAmount(discounts, bill.seasonalDiscount)
                    && addAmount(discounts, bill.specialDiscount) && discountsValid;
            }
            cells << money(bill.totalPayable);
            row(cells);
        }

        QString summary = QStringLiteral("Orders  %1    Payable total  %2")
            .arg(table_->rowCount()).arg(displayTotal(totalValid, total));
        if (cash)
            summary += QStringLiteral("    %1 to %2").arg(range.start.toString(Qt::ISODate), range.end.toString(Qt::ISODate));
        if (sales)
            summary += QStringLiteral("    Combined discounts  %1").arg(displayTotal(discountsValid, discounts));
        summary_->setText(summary);
    }

    void showRanking(const store::CheckoutStorage& storage, bool mostSold, const QDir& dir)
    {
        QStringList headers{QStringLiteral("Rank"), QStringLiteral("Product ID"), QStringLiteral("Product")};
        if (mostSold)
            headers << QStringLiteral("Quantity sold");
        headers << QStringLiteral("Stock on hand");
        columns(headers);

        ProductCatalog catalog;
        QString error;
        if (!catalog.load(dir.filePath(QStringLiteral("Product.txt")), &error)) {
            summary_->setText(error);
            return;
        }

        const Unit unit = units_->currentIndex() == 0 ? Unit::Count : Unit::Weight;
        const auto sold = mostSold ? storage.soldQuantities() : QHash<QString, qint64>{};
        QVector<Product> ranked;
        for (const Product& product : catalog.products()) {
            if (product.unit == unit && (!mostSold || sold.value(product.id) > 0))
                ranked.push_back(product);
        }

        const auto score = [&](const Product& product) {
            return mostSold ? sold.value(product.id) : product.stockMilli;
        };
        std::sort(ranked.begin(), ranked.end(), [&](const Product& first, const Product& second) {
            if (score(first) != score(second))
                return score(first) > score(second);
            return first.id < second.id;
        });

        for (int index = 0; index < ranked.size() && index < 10; ++index) {
            const Product& product = ranked.at(index);
            QStringList cells{QString::number(index + 1), product.id, product.name};
            if (mostSold)
                cells << formatQuantityMilli(sold.value(product.id), unit);
            cells << formatQuantityMilli(product.stockMilli, unit);
            row(cells);
        }
        summary_->setText(table_->rowCount() == 0
            ? QStringLiteral("No matching products.") : QStringLiteral("Items shown  %1").arg(table_->rowCount()));
    }

    QString dataDir_;
    QComboBox* report_ = nullptr;
    QComboBox* period_ = nullptr;
    QComboBox* units_ = nullptr;
    QWidget* dates_ = nullptr;
    QDateEdit* from_ = nullptr;
    QDateEdit* to_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* summary_ = nullptr;
};

} // namespace

QWidget* makeReportsPage(const QString& dataDir, QWidget* parent)
{
    return new ReportsPage(dataDir, parent);
}

void openReports(QWidget* parent, const QString& dataDir)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Reports"));
    dialog.resize(1050, 550);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(makeReportsPage(dataDir, &dialog));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

} // namespace shop
