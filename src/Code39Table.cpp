#include "Code39Table.h"

#include <QtGlobal>

#include <array>

namespace {

struct Code39Entry {
    char character;
    const char* pattern;
};

// Canonical Code 39 encodings from ISO/IEC 16388.
constexpr std::array<Code39Entry, 11> code39Entries{{
    {'0', "nnnwwnwnn"},
    {'1', "wnnwnnnnw"},
    {'2', "nnwwnnnnw"},
    {'3', "wnwwnnnnn"},
    {'4', "nnnwwnnnw"},
    {'5', "wnnwwnnnn"},
    {'6', "nnwwwnnnn"},
    {'7', "nnnwnnwnw"},
    {'8', "wnnwnnwnn"},
    {'9', "nnwwnnwnn"},
    {'*', "nwnnwnwnn"},
}};

void assertValidPatterns()
{
#ifndef QT_NO_DEBUG
    for (const Code39Entry& entry : code39Entries) {
        int length = 0;
        int wideCount = 0;

        for (const char* element = entry.pattern; *element != '\0'; ++element) {
            Q_ASSERT(*element == 'n' || *element == 'w');
            ++length;
            if (*element == 'w') {
                ++wideCount;
            }
        }

        Q_ASSERT(length == 9);
        Q_ASSERT(wideCount == 3);
    }
#endif
}

}

namespace shop {

QString Code39Table::patternFor(QChar ch)
{
    assertValidPatterns();

    for (const Code39Entry& entry : code39Entries) {
        if (ch == QChar::fromLatin1(entry.character)) {
            return QString::fromLatin1(entry.pattern);
        }
    }

    return QString();
}

QChar Code39Table::characterForPattern(const QString& pattern)
{
    assertValidPatterns();

    for (const Code39Entry& entry : code39Entries) {
        if (pattern == QString::fromLatin1(entry.pattern)) {
            return QChar::fromLatin1(entry.character);
        }
    }

    return QChar();
}

}
