#pragma once

#include <QChar>
#include <QString>

namespace shop {

class Code39Table {
public:
    static QString patternFor(QChar ch);
    static QChar characterForPattern(const QString& pattern);
};

}