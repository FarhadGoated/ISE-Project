#include "BillPdf.h"
#include "BillCalculator.h"
#include "BillStorage.h"

#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QStringList>
#include <QtGlobal>

namespace shop {

    namespace {

        constexpr int receiptWidth = 48;

        QString centered(const QString& text)
        {
            const int padding = qMax(0, (receiptWidth - static_cast<int>(text.size())) / 2);
            return QString(padding, QLatin1Char(' ')) + text;
        }

        QString rule()
        {
            return QString(receiptWidth, QLatin1Char('-'));
        }

        QString twoColumn(const QString& label, const QString& value)
        {
            const int gap = qMax(1, receiptWidth - static_cast<int>(label.size())
                - static_cast<int>(value.size()));
            return label + QString(gap, QLatin1Char(' ')) + value;
        }

        QString money(qint64 paisa)
        {
            const bool negative = paisa < 0;
            const qint64 absolute = negative ? -paisa : paisa;
            const QString whole = QString::number(absolute / 100);
            const QString fraction = QStringLiteral("%1").arg(absolute % 100, 2, 10, QLatin1Char('0'));
            return (negative ? QStringLiteral("-") : QString()) + whole + QStringLiteral(".") + fraction;
        }

        QString quantityText(qint64 quantityMilli)
        {
            const qint64 whole = quantityMilli / 1000;
            const qint64 rest = quantityMilli % 1000;
            if (rest == 0) {
                return QString::number(whole);
            }
            return QStringLiteral("%1.%2").arg(whole).arg(rest, 3, 10, QLatin1Char('0'));
        }

        QString capitalized(const QString& text)
        {
            if (text.isEmpty()) {
                return text;
            }
            return text.left(1).toUpper() + text.mid(1);
        }

        QStringList buildLines(const Bill& bill)
        {
            QStringList lines;

            lines << centered(QStringLiteral("Super Duper Ultra Mega Store"));
            lines << rule();
            lines << twoColumn(QStringLiteral("Bill ID"), bill.billId);
            lines << twoColumn(QStringLiteral("Order ID"), bill.orderId);
            lines << twoColumn(QStringLiteral("Date"), bill.billDate.toString(Qt::ISODate));
            lines << twoColumn(QStringLiteral("Time"), bill.billTime.toString(Qt::ISODate));
            lines << twoColumn(QStringLiteral("Customer"), bill.customerName);
            lines << rule();
            lines << QStringLiteral("Item").leftJustified(20)
                + QStringLiteral("Qty").rightJustified(8)
                + QStringLiteral("Price").rightJustified(9)
                + QStringLiteral("Total").rightJustified(11);
            lines << rule();

            for (const BillLine& line : bill.lines) {
                const OrderItem& item = line.item;
                lines << item.product.acronym.left(20).leftJustified(20)
                    + quantityText(item.quantityMilli).rightJustified(8)
                    + money(item.product.pricePaisa).rightJustified(9)
                    + money(line.amounts.subtotalPaisa).rightJustified(11);
            }

            lines << rule();
            lines << twoColumn(QStringLiteral("Subtotal"), money(bill.totals.subtotalPaisa));
            lines << twoColumn(QStringLiteral("Product discount"), money(bill.totals.discountPaisa));
            lines << twoColumn(QStringLiteral("Seasonal discount"), money(bill.seasonalDiscountPaisa));
            lines << twoColumn(QStringLiteral("Special discount"), money(bill.specialDiscountPaisa));
            lines << twoColumn(QStringLiteral("Tax"), money(bill.totals.taxPaisa));
            lines << rule();
            lines << twoColumn(QStringLiteral("PAYABLE (PKR)"), money(bill.totals.payablePaisa));
            lines << rule();

            QString method = capitalized(bill.paymentMode);
            if (!bill.paymentProvider.isEmpty()) {
                method += QStringLiteral(" - ") + bill.paymentProvider;
            }
            if (!method.isEmpty()) lines << twoColumn(QStringLiteral("Payment"), method);
            lines << twoColumn(QStringLiteral("Received"), money(bill.amountReceivedPaisa));
            lines << twoColumn(QStringLiteral("Balance returned"), money(bill.balanceReturnedPaisa));
            lines << twoColumn(QStringLiteral("Status"), bill.paymentStatus);

            return lines;
        }

    }  // namespace

    bool exportBillPdf(const Bill& bill, const QString& destination, QString& error)
    {
        error.clear();

        if (bill.lines.isEmpty()) {
            error = QStringLiteral("There is no bill to print.");
            return false;
        }
        if (destination.isEmpty()
            || QFileInfo(destination).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) != 0) {
            error = QStringLiteral("Choose a file name that ends in .pdf.");
            return false;
        }

        const QStringList lines = buildLines(bill);

        QPdfWriter writer(destination);
        writer.setPageSize(QPageSize(QPageSize::A5));
        writer.setPageMargins(QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter);
        writer.setResolution(300);

        QPainter painter;
        if (!painter.begin(&writer)) {
            error = QStringLiteral("Could not create the PDF file.");
            return false;
        }

        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSize(9);
        painter.setFont(font);

        const QFontMetrics metrics = painter.fontMetrics();
        const int lineHeight = metrics.lineSpacing();
        const int pageHeight = writer.height();

        int y = 0;
        for (const QString& line : lines) {
            if (y + lineHeight > pageHeight) {
                writer.newPage();
                y = 0;
            }
            painter.drawText(0, y + metrics.ascent(), line);
            y += lineHeight;
        }

        if (!painter.end()) {
            error = QStringLiteral("Could not finish writing the PDF file.");
            return false;
        }
        return true;
    }

    bool offerBillPdf(QWidget* parent, const Bill& bill)
    {
        const QString name = bill.billId.isEmpty()
            ? QStringLiteral("Bill")
            : QStringLiteral("Bill-") + bill.billId;

        QString path = QFileDialog::getSaveFileName(
            parent, QStringLiteral("Save bill as PDF"),
            QDir(QDir::homePath()).filePath(name + QStringLiteral(".pdf")),
            QStringLiteral("PDF files (*.pdf)"));
        if (path.isEmpty()) {
            return false;
        }
        if (QFileInfo(path).suffix().isEmpty()) {
            path += QStringLiteral(".pdf");
        }

        QString error;
        if (!exportBillPdf(bill, path, error)) {
            QMessageBox::warning(parent, QStringLiteral("Save PDF"), error);
            return false;
        }

        QMessageBox::information(parent, QStringLiteral("Save PDF"),
            QStringLiteral("Saved ") + path);
        return true;
    }

    namespace {

        class ReprintDialog : public QDialog {
        public:
            ReprintDialog(QWidget* parent, const QString& dataDir)
                : QDialog(parent), dataDir_(dataDir)
            {
                setWindowTitle(QStringLiteral("Reprint bill"));

                auto* layout = new QVBoxLayout(this);

                auto* row = new QHBoxLayout();
                kindCombo_ = new QComboBox(this);
                kindCombo_->addItems(QStringList{ QStringLiteral("Order ID"), QStringLiteral("Bill ID") });
                idEdit_ = new QLineEdit(this);
                idEdit_->setPlaceholderText(QStringLiteral("Enter the ID"));
                row->addWidget(kindCombo_);
                row->addWidget(idEdit_, 1);
                layout->addLayout(row);

                messageLabel_ = new QLabel(this);
                layout->addWidget(messageLabel_);

                auto* buttons = new QHBoxLayout();
                auto* printButton = new QPushButton(QStringLiteral("Print to PDF"), this);
                auto* cancelButton = new QPushButton(QStringLiteral("Cancel"), this);
                buttons->addStretch(1);
                buttons->addWidget(printButton);
                buttons->addWidget(cancelButton);
                layout->addLayout(buttons);

                connect(printButton, &QPushButton::clicked, this, [this] { onPrint(); });
                connect(idEdit_, &QLineEdit::returnPressed, this, [this] { onPrint(); });
                connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
            }

        private:
            void onPrint()
            {
                const QString id = idEdit_->text().trimmed();
                if (id.isEmpty()) {
                    messageLabel_->setText(QStringLiteral("Enter an Order ID or a Bill ID."));
                    return;
                }

                const bool byOrder = (kindCombo_->currentIndex() == 0);
                Bill bill;
                QString error;
                const bool loaded = byOrder
                    ? loadActiveBill(dataDir_, id, bill, error)
                    : loadBill(dataDir_, id, bill, error);
                if (!loaded) {
                    messageLabel_->setText(error.isEmpty()
                        ? QStringLiteral("Could not find that bill.")
                        : error);
                    return;
                }

                if (byOrder && bill.orderId.isEmpty()) {
                    bill.orderId = id;
                }
                if (!byOrder && bill.billId.isEmpty()) {
                    bill.billId = id;
                }

                messageLabel_->clear();
                if (offerBillPdf(this, bill)) {
                    accept();
                }
            }

            QString dataDir_;
            QComboBox* kindCombo_ = nullptr;
            QLineEdit* idEdit_ = nullptr;
            QLabel* messageLabel_ = nullptr;
        };

    }  // namespace

    bool reprintBill(QWidget* parent, const QString& dataDir)
    {
        ReprintDialog dialog(parent, dataDir);
        return dialog.exec() == QDialog::Accepted;
    }

} // namespace shop