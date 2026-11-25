#pragma once
#include "Bill.h"

namespace shop {

    struct DiscountRates {
        int seasonalBasisPoints = 0;
        int specialBasisPoints = 0;
    };

    class BillCalculator {
    public:
        static bool calculate(const Order& order, Bill& bill,
            QString* error = nullptr);
        static bool calculate(const Order& order, const DiscountRates& rates,
            Bill& bill, QString* error = nullptr);

        static int clampSpecialBasisPoints(int basisPoints);
        static bool parsePercentBasisPoints(const QString& text, int& basisPoints);
        static QString formatPercentBasisPoints(int basisPoints);
    };
}