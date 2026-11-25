#include "PaymentDialog.h"
#include <limits>

#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
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

        class PaymentDialog : public QDialog {
        public:
            PaymentDialog(QWidget* parent, qint64 payablePaisa, qint64 alreadyPaidPaisa)
                : QDialog(parent), payablePaisa_(payablePaisa),
                  creditPaisa_(qBound<qint64>(0, alreadyPaidPaisa, payablePaisa)),
                  cashDue_(payablePaisa - creditPaisa_)
            {
                setWindowTitle(QStringLiteral("Payment"));

                auto* layout = new QVBoxLayout(this);
                form_ = new QFormLayout();
                layout->addLayout(form_);

                form_->addRow(QStringLiteral("Payable"),
                    new QLabel(QStringLiteral("PKR ") + formatPkr(payablePaisa), this));

                if (alreadyPaidPaisa >= 0) {
                    const qint64 difference = payablePaisa - alreadyPaidPaisa;
                    QString settlement = QStringLiteral("No difference");
                    if (difference > 0) {
                        settlement = QStringLiteral("Collect PKR %1 more").arg(formatPkr(difference));
                    }
                    else if (difference < 0) {
                        settlement = QStringLiteral("Refund PKR %1").arg(formatPkr(-difference));
                    }
                    form_->addRow(QStringLiteral("Already paid"),
                        new QLabel(QStringLiteral("PKR ") + formatPkr(alreadyPaidPaisa), this));
                    form_->addRow(QStringLiteral("Settlement"), new QLabel(settlement, this));
                }

                modeCombo_ = new QComboBox(this);
                modeCombo_->addItems(QStringList{ QStringLiteral("Cash"), QStringLiteral("Card"),
                                                 QStringLiteral("Wallet") });
                form_->addRow(QStringLiteral("Payment method"), modeCombo_);

                providerCombo_ = new QComboBox(this);
                form_->addRow(QStringLiteral("Provider"), providerCombo_);

                otherEdit_ = new QLineEdit(this);
                otherEdit_->setPlaceholderText(QStringLiteral("Provider name"));
                form_->addRow(QStringLiteral("Other provider"), otherEdit_);

                receivedEdit_ = new QLineEdit(this);
                receivedEdit_->setPlaceholderText(QStringLiteral("e.g. 500 or 500.50"));
                form_->addRow(alreadyPaidPaisa >= 0 ? QStringLiteral("Additional cash tendered")
                                                     : QStringLiteral("Amount received"), receivedEdit_);
                if (!cashDue_) receivedEdit_->setText(QStringLiteral("0.00"));

                changeLabel_ = new QLabel(this);
                form_->addRow(QStringLiteral("Balance to return"), changeLabel_);

                messageLabel_ = new QLabel(this);
                layout->addWidget(messageLabel_);

                auto* buttons = new QHBoxLayout();
                auto* confirmButton = new QPushButton(QStringLiteral("Confirm payment"), this);
                auto* cancelButton = new QPushButton(QStringLiteral("Cancel"), this);
                buttons->addStretch(1);
                buttons->addWidget(confirmButton);
                buttons->addWidget(cancelButton);
                layout->addLayout(buttons);

                connect(modeCombo_, &QComboBox::currentIndexChanged, this, [this](int) { refresh(); });
                connect(providerCombo_, &QComboBox::currentIndexChanged, this, [this](int) { refresh(); });
                connect(receivedEdit_, &QLineEdit::textChanged, this, [this](const QString&) { refresh(); });
                connect(confirmButton, &QPushButton::clicked, this, [this] { onConfirm(); });
                connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

                refresh();
            }

            const PaymentInput& result() const { return result_; }

        private:
            void refresh()
            {
                const QString mode = modeCombo_->currentText();
                const bool cash = (mode == QLatin1String("Cash"));

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
                form_->setRowVisible(receivedEdit_, cash && cashDue_ > 0);

                if (cash) {
                    qint64 entered = 0;
                    if (parsePkr(receivedEdit_->text(), entered) && entered >= cashDue_) {
                        changeLabel_->setText(QStringLiteral("PKR ") + formatPkr(entered - cashDue_));
                    }
                    else {
                        changeLabel_->setText(QStringLiteral("-"));
                    }
                }
                else {
                    changeLabel_->setText(QStringLiteral("PKR 0.00"));
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
            qint64 creditPaisa_ = 0;
            qint64 cashDue_ = 0;
            QString lastMode_;
            PaymentInput result_;

            QFormLayout* form_ = nullptr;
            QComboBox* modeCombo_ = nullptr;
            QComboBox* providerCombo_ = nullptr;
            QLineEdit* otherEdit_ = nullptr;
            QLineEdit* receivedEdit_ = nullptr;
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

    bool collectPayment(QWidget* parent, qint64 payablePaisa, PaymentInput& payment)
    {
        return collectPayment(parent, payablePaisa, -1, payment);
    }

} // namespace shop