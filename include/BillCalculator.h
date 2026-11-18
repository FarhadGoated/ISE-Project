#pragma once
#include "Bill.h"

namespace shop {
class BillCalculator {
public:
    static bool calculate(const Order& order, Bill& bill,
                          QString* error = nullptr);
};
}
