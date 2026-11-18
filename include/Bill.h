#pragma once
#include "Order.h"

namespace shop {
struct Amounts {
    qint64 subtotalPaisa = 0;
    qint64 taxPaisa = 0;
    qint64 discountPaisa = 0;
    qint64 payablePaisa = 0;
};

struct BillLine {
    OrderItem item;
    Amounts amounts;
};

struct Bill {
    QVector<BillLine> lines;
    Amounts totals;
};
}
