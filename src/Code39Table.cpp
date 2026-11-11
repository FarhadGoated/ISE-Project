#include "Code39Table.h"

namespace shop {

QString Code39Table::patternFor(QChar)
{
    return {};
}

QChar Code39Table::characterForPattern(const QString&)
{
    return {};
}

}