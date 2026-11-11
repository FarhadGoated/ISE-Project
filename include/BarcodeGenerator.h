#pragma once

#include <QImage>
#include <QString>

namespace shop {

class BarcodeGenerator {
public:
    static QImage generateCode39(const QString& digits,
                                 QString* error = nullptr);
};

}