#include "PaymentDialog.h"

#include "BillCalculator.h"
#include "ManagerWindow.h"
#include <limits>

#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <utility>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QVBoxLayout>

namespace shop {

    namespace {

        bool onlyDigits(const QString& text)
        {
            for (QChar ch : text) {
                if (ch.unicode() < '0' || ch.unicode() > '9') {
                    return false;
                }
            }
            return true;
        }

        bool parsePkr(const QString& text, qint64& paisa)
        {
            const QString value = text.trimmed();
            if (value.isEmpty()) {
                return false;
            }

            const QStringList parts = value.split(QLatin1Char('.'));
            if (parts.size() > 2) {
                return false;
            }

            const QString whole = parts[0];
            const QString fraction = parts.size() == 2 ? parts[1] : QString();
            if (whole.isEmpty() || whole.size() > 12 || fraction.size() > 2) {
                return false;
            }
            if (!onlyDigits(whole) || !onlyDigits(fraction)) {
                return false;
            }

            qint64 total = whole.toLongLong() * 100;
            if (!fraction.isEmpty()) {
                qint64 fractionPaisa = fraction.toLongLong();
                if (fraction.size() == 1) {
                    fractionPaisa *= 10;
                }
                total += fractionPaisa;
            }

            paisa = total;
            return true;
        }

        QString formatPkr(qint64 paisa)
        {
            const bool negative = paisa < 0;
            const qint64 absolute = negative ? -paisa : paisa;
            const QString whole = QString::number(absolute / 100);
            const QString fraction = QStringLiteral("%1").arg(absolute % 100, 2, 10, QLatin1Char('0'));
            return (negative ? QStringLiteral("-") : QString()) + whole + QStringLiteral(".") + fraction;
        }

        QStringList providersFor(const QString& mode)
        {
            if (mode == QLatin1String("Card")) {
                return QStringList{ QStringLiteral("Visa"), QStringLiteral("Mastercard"),
                                   QStringLiteral("UnionPay"), QStringLiteral("Other") };
            }
            if (mode == QLatin1String("Wallet")) {
                return QStringList{ QStringLiteral("JazzCash"), QStringLiteral("Easypaisa"),
                                   QStringLiteral("SadaPay"), QStringLiteral("Other") };
            }
            return QStringList();
        }

        // The calculator applies the same discount base and paisa rounding as the receipt.
        bool findCashDiscount(const Order& order, const Bill& bill, qint64 targetPaisa, Bill& result)
        {
            int low = bill.specialBasisPoints + 1;
            int high = BillCalculator::clampSpecialBasisPoints(10000);
            if (low > high) return false;

            DiscountRates rates{bill.seasonalBasisPoints, high};
            if (!BillCalculator::calculate(order, rates, result) ||
                result.totals.payablePaisa > targetPaisa) return false;

            while (low < high) {
                const int middle = low + (high - low) / 2;
                rates.specialBasisPoints = middle;
                Bill candidate;
                if (BillCalculator::calculate(order, rates, candidate) &&
                    candidate.totals.payablePaisa <= targetPaisa) {
                    high = middle;
                    result = std::move(candidate);
                } else {
                    low = middle + 1;
                }
            }
            return true;
        }

        class PaymentDialog : public QDialog {
        public:
            PaymentDialog(QWidget* parent, qint64 payablePaisa, qint64 alreadyPaidPaisa,
                          const Order* order = nullptr, const Bill* originalBill = nullptr)
                : QDialog(parent), payablePaisa_(payablePaisa), originalPayablePaisa_(payablePaisa),
                  alreadyPaidPaisa_(alreadyPaidPaisa), order_(order), originalBill_(originalBill)
            {
                setWindowTitle(QStringLiteral("Payment"));
                resize(460, 330);
                setMinimumWidth(420);

                auto* layout = new QVBoxLayout(this);
                layout->setContentsMargins(20, 18, 20, 16);
                layout->setSpacing(12);
                auto* heading = new QLabel(QStringLiteral("Payment"), this);
                heading->setObjectName(QStringLiteral("pageTitle"));
                layout->addWidget(heading);

                auto* totalPanel = new QFrame(this);
                totalPanel->setObjectName(QStringLiteral("payablePanel"));
                auto* totalLayout = new QVBoxLayout(totalPanel);
                totalLayout->setContentsMargins(14, 10, 14, 12);
                totalLayout->setSpacing(3);
                auto* caption = new QLabel(alreadyPaidPaisa >= 0
                    ? QStringLiteral("Revised bill total") : QStringLiteral("Amount due"), totalPanel);
                caption->setProperty("muted", true);
                totalLabel_ = new QLabel(totalPanel);
                totalLabel_->setObjectName(QStringLiteral("payableAmount"));
                totalLayout->addWidget(caption);
                totalLayout->addWidget(totalLabel_);
                layout->addWidget(totalPanel);

                auto* details = new QFrame(this);
                details->setProperty("panel", true);
                form_ = new QFormLayout(details);
                form_->setContentsMargins(14, 12, 14, 12);
                form_->setVerticalSpacing(10);
                form_->setHorizontalSpacing(12);
                form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
                layout->addWidget(details);

                if (alreadyPaidPaisa >= 0) {
                    form_->addRow(QStringLiteral("Already paid"),
                        new QLabel(QStringLiteral("PKR ") + formatPkr(alreadyPaidPaisa), this));
                    settlementLabel_ = new QLabel(this);
                    form_->addRow(QStringLiteral("Settlement"), settlementLabel_);
                }

                modeCombo_ = new QComboBox(this);
                modeCombo_->addItems(QStringList{ QStringLiteral("Cash"), QStringLiteral("Card"),
                                                 QStringLiteral("Wallet") });
                modeCombo_->setMinimumHeight(34);
                form_->addRow(QStringLiteral("&Payment method"), modeCombo_);

                providerCombo_ = new QComboBox(this);
                providerCombo_->setMinimumHeight(34);
                form_->addRow(QStringLiteral("Provider"), providerCombo_);

                otherEdit_ = new QLineEdit(this);
                otherEdit_->setPlaceholderText(QStringLiteral("Provider name"));
                form_->addRow(QStringLiteral("Other provider"), otherEdit_);

                receivedEdit_ = new QLineEdit(this);
                receivedEdit_->setPlaceholderText(QStringLiteral("e.g. 500 or 500.50"));
                form_->addRow(alreadyPaidPaisa >= 0 ? QStringLiteral("Additional cash tendered")
                    : QStringLiteral("Amount received"), receivedEdit_);


                changePanel_ = new QFrame(this);
                changePanel_->setProperty("panel", true);
                auto* changeRow = new QHBoxLayout(changePanel_);
                changeRow->setContentsMargins(14, 8, 14, 8);
                changeCaption_ = new QLabel(changePanel_);
                changeCaption_->setProperty("muted", true);
                changeLabel_ = new QLabel(changePanel_);
                changeLabel_->setProperty("heading", true);
                changeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                changeRow->addWidget(changeCaption_, 1);
                changeRow->addWidget(changeLabel_);
                layout->addWidget(changePanel_);
                approveDiscount_ = new QPushButton(QStringLiteral("Request manager discount"), this);
                approveDiscount_->setAutoDefault(false);
                layout->addWidget(approveDiscount_);
                discountLabel_ = new QLabel(this);
                discountLabel_->setProperty("muted", true);
                layout->addWidget(discountLabel_);
                connect(approveDiscount_, &QPushButton::clicked, this, [this] { approveShortfall(); });

                messageLabel_ = new QLabel(this);
                messageLabel_->setWordWrap(true);
                messageLabel_->setStyleSheet(QStringLiteral("color: #f0aba0;"));
                layout->addWidget(messageLabel_);
                layout->addStretch();

                auto* buttons = new QHBoxLayout();
                auto* confirmButton = new QPushButton(QStringLiteral("Confirm payment"), this);
                auto* cancelButton = new QPushButton(QStringLiteral("Cancel"), this);
                buttons->addStretch(1);
                confirmButton->setProperty("role", "primary");
                confirmButton->setMinimumHeight(40);
                confirmButton->setDefault(true);
                cancelButton->setAutoDefault(false);
                buttons->addWidget(cancelButton);
                buttons->addWidget(confirmButton);
                layout->addLayout(buttons);

                connect(modeCombo_, &QComboBox::currentIndexChanged, this, [this](int) { refresh(); });
                connect(providerCombo_, &QComboBox::currentIndexChanged, this, [this](int) { refresh(); });
                connect(receivedEdit_, &QLineEdit::textChanged, this, [this](const QString&) { refresh(); });
                connect(confirmButton, &QPushButton::clicked, this, [this] { onConfirm(); });
                connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

                updatePayable(payablePaisa);
                if (originalCashDue() == 0) receivedEdit_->setText(QStringLiteral("0.00"));
                refresh();
                if (cashDue_ > 0) receivedEdit_->setFocus();
            }

            const PaymentInput& result() const { return result_; }
            void applyDiscount(Bill& bill) const {
                if (hasDiscount_) bill = discountedBill_;
            }

        private:
            qint64 originalCashDue() const
            {
                return originalPayablePaisa_ - qBound<qint64>(0, alreadyPaidPaisa_, originalPayablePaisa_);
            }

            void updatePayable(qint64 payable)
            {
                payablePaisa_ = payable;
                creditPaisa_ = qBound<qint64>(0, alreadyPaidPaisa_, payable);
                cashDue_ = payable - creditPaisa_;
                totalLabel_->setText(QStringLiteral("PKR %1").arg(formatPkr(payable)));
                if (settlementLabel_) {
                    const qint64 difference = payable - alreadyPaidPaisa_;
                    settlementLabel_->setText(difference > 0
                        ? QStringLiteral("Collect PKR %1 more").arg(formatPkr(difference))
                        : difference < 0 ? QStringLiteral("Refund PKR %1").arg(formatPkr(-difference))
                                         : QStringLiteral("No difference"));
                }
            }

            bool discountForTender(qint64 tender, Bill& candidate) const
            {
                if (!order_ || !originalBill_ || tender >= cashDue_) return false;
                const Bill& current = hasDiscount_ ? discountedBill_ : *originalBill_;
                return findCashDiscount(*order_, current, creditPaisa_ + tender, candidate);
            }

            void approveShortfall()
            {
                qint64 tender = 0;
                Bill candidate;
                if (modeCombo_->currentText() != QLatin1String("Cash") ||
                    !parsePkr(receivedEdit_->text(), tender) || !discountForTender(tender, candidate)) return;

                const QString rate = BillCalculator::formatPercentBasisPoints(candidate.specialBasisPoints);
                const QString prompt = QStringLiteral("Approve a %1% special discount? The new total is PKR %2.")
                    .arg(rate, formatPkr(candidate.totals.payablePaisa));
                if (QMessageBox::question(this, QStringLiteral("Special discount"), prompt) != QMessageBox::Yes ||
                    !authorizeManager(this, QStringLiteral("Approve %1% special discount").arg(rate))) return;

                discountedBill_ = std::move(candidate);
                hasDiscount_ = true;
                updatePayable(discountedBill_.totals.payablePaisa);
                refresh();
            }

            void refresh()
            {
                const QString mode = modeCombo_->currentText();
                const bool cash = (mode == QLatin1String("Cash"));

                if (!cash && hasDiscount_) {
                    hasDiscount_ = false;
                    updatePayable(originalPayablePaisa_);
                }
                if (mode != lastMode_) {
                    lastMode_ = mode;
                    const QSignalBlocker blocker(providerCombo_);
                    providerCombo_->clear();
                    providerCombo_->addItems(providersFor(mode));
                }

                const bool needsProvider = providerCombo_->count() > 0;
                const bool other = needsProvider
                    && providerCombo_->currentText() == QLatin1String("Other");

                form_->setRowVisible(providerCombo_, needsProvider);
                form_->setRowVisible(otherEdit_, other);
                form_->setRowVisible(receivedEdit_, cash && originalCashDue() > 0);
                changePanel_->setVisible(cash);

                qint64 entered = 0;
                const bool enteredCash = cash && parsePkr(receivedEdit_->text(), entered);
                const bool shortfall = enteredCash && entered < cashDue_;
                changeCaption_->setText(shortfall ? QStringLiteral("Still due") : QStringLiteral("Change to return"));
                changeLabel_->setText(enteredCash
                    ? QStringLiteral("PKR %1").arg(formatPkr(shortfall ? cashDue_ - entered : entered - cashDue_))
                    : QStringLiteral("\u2014"));

                Bill candidate;
                approveDiscount_->setVisible(shortfall && discountForTender(entered, candidate));
                discountLabel_->setVisible(hasDiscount_);
                if (hasDiscount_) {
                    discountLabel_->setText(QStringLiteral("%1% special discount approved")
                        .arg(BillCalculator::formatPercentBasisPoints(discountedBill_.specialBasisPoints)));
                }

                messageLabel_->clear();
            }

            void onConfirm()
            {
                const QString mode = modeCombo_->currentText();
                PaymentInput settled;
                settled.mode = mode;

                if (mode == QLatin1String("Cash")) {
                    qint64 entered = 0;
                    if (!parsePkr(receivedEdit_->text(), entered)) {
                        messageLabel_->setText(QStringLiteral("Enter the amount received, for example 500 or 500.50."));
                        return;
                    }
                    if (entered < cashDue_) {
                        messageLabel_->setText(QStringLiteral("The amount received must cover PKR %1.")
                            .arg(formatPkr(cashDue_)));
                        return;
                    }
                    if (entered > std::numeric_limits<qint64>::max() - creditPaisa_) {
                        messageLabel_->setText("Amount is too large."); return;
                    }
                    settled.receivedPaisa = creditPaisa_ + entered;
                    settled.changePaisa = entered - cashDue_;
                }
                else {
                    QString provider = providerCombo_->currentText();
                    if (provider == QLatin1String("Other")) {
                        provider = otherEdit_->text().trimmed();
                        if (provider.isEmpty()) {
                            messageLabel_->setText(QStringLiteral("Enter the provider name."));
                            return;
                        }
                    }
                    settled.provider = provider;
                    settled.receivedPaisa = payablePaisa_;
                    settled.changePaisa = 0;
                }

                result_ = settled;
                accept();
            }

            qint64 payablePaisa_ = 0;
            qint64 originalPayablePaisa_ = 0;
            qint64 alreadyPaidPaisa_ = -1;
            const Order* order_ = nullptr;
            const Bill* originalBill_ = nullptr;
            Bill discountedBill_;
            bool hasDiscount_ = false;
            qint64 creditPaisa_ = 0;
            qint64 cashDue_ = 0;
            QString lastMode_;
            PaymentInput result_;

            QFormLayout* form_ = nullptr;
            QComboBox* modeCombo_ = nullptr;
            QComboBox* providerCombo_ = nullptr;
            QLineEdit* otherEdit_ = nullptr;
            QLineEdit* receivedEdit_ = nullptr;
            QFrame* changePanel_ = nullptr;
            QLabel* totalLabel_ = nullptr;
            QLabel* settlementLabel_ = nullptr;
            QLabel* changeCaption_ = nullptr;
            QLabel* discountLabel_ = nullptr;
            QPushButton* approveDiscount_ = nullptr;
            QLabel* changeLabel_ = nullptr;
            QLabel* messageLabel_ = nullptr;
        };

    }  // namespace

    bool collectPayment(QWidget* parent, qint64 payablePaisa, qint64 alreadyPaidPaisa,
        PaymentInput& payment)
    {
        if (payablePaisa < 0) {
            return false;
        }

        PaymentDialog dialog(parent, payablePaisa, alreadyPaidPaisa);
        if (dialog.exec() != QDialog::Accepted) {
            return false;
        }

        payment = dialog.result();
        return true;
    }

    bool collectPayment(QWidget* parent, const Order& order, Bill& bill,
        qint64 alreadyPaidPaisa, PaymentInput& payment)
    {
        if (bill.totals.payablePaisa < 0) return false;
        PaymentDialog dialog(parent, bill.totals.payablePaisa, alreadyPaidPaisa, &order, &bill);
        if (dialog.exec() != QDialog::Accepted) return false;

        payment = dialog.result();
        dialog.applyDiscount(bill);
        return true;
    }

    bool collectPayment(QWidget* parent, qint64 payablePaisa, PaymentInput& payment)
    {
        return collectPayment(parent, payablePaisa, -1, payment);
    }

} // namespace shop