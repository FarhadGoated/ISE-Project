#pragma once

#include <QString>
#include <QtGlobal>

class QWidget;

namespace shop {

    struct PaymentInput {
        QString mode;
        QString provider;
        qint64 receivedPaisa = 0;
        qint64 changePaisa = 0;
    };

    bool collectPayment(QWidget* parent, qint64 payablePaisa, PaymentInput& payment);

    bool collectPayment(QWidget* parent, qint64 payablePaisa, qint64 alreadyPaidPaisa,
        PaymentInput& payment);

} // namespace shop