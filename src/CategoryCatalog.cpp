// CategoryCatalog.cpp
#include "CategoryCatalog.h"
#include "ProductCatalog.h"   // tokenizeRecord() and quoteField()

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>

namespace shop {

namespace {

void setError(QString* error, const QString& message)
{
    if (error) *error = message;
}

bool isAsciiDigit(QChar c)
{
    return c.unicode() >= '0' && c.unicode() <= '9';
}

// Rules for one category; the message has no line number.
bool checkCategory(const Category& c, QString& err)
{
    if (c.id.size() != 2 || !isAsciiDigit(c.id.at(0)) || !isAsciiDigit(c.id.at(1))) {
        err = QStringLiteral("category ID \"%1\" must be exactly two digits").arg(c.id);
        return false;
    }
    if (c.name.isEmpty()) { err = QStringLiteral("category name is empty"); return false; }
    if (c.acronym.isEmpty() || c.acronym.size() > 8) {
        err = QStringLiteral("acronym must be 1 to 8 characters, got \"%1\"").arg(c.acronym);
        return false;
    }
    return true;
}

} // namespace

bool CategoryCatalog::load(const QString& categoryPath, QString* error)
{
    const QFileInfo info(categoryPath);
    filePath_ = info.absoluteFilePath();   // remembered even if the file is not there yet

    if (!info.exists()) {
        categories_.clear();
        if (error) error->clear();
        return true;
    }

    QFile file(categoryPath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot open %1: %2").arg(categoryPath, file.errorString()));
        return false;
    }
    QByteArray bytes = file.readAll();
    file.close();
    if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);   // UTF-8 BOM

    // Build in a temporary so a bad file never replaces good data.
    CategoryCatalog loaded;
    const QList<QByteArray> rawLines = bytes.split('\n');
    for (int index = 0; index < rawLines.size(); ++index) {
        const int lineNo = index + 1;
        const auto fail = [&](const QString& message) {
            setError(error, QStringLiteral("%1 line %2: %3").arg(info.fileName()).arg(lineNo).arg(message));
            return false;
        };

        const QString decoded = QString::fromUtf8(rawLines.at(index));
        if (decoded.contains(QChar(QChar::ReplacementCharacter)))
            return fail(QStringLiteral("invalid UTF-8 text"));

        QString line = decoded.trimmed();
        if (line.isEmpty()) continue;
        if (!line.endsWith(QLatin1Char('#')))
            return fail(QStringLiteral("record must end with #"));
        line.chop(1);

        QStringList fields;
        QString problem;
        if (!tokenizeRecord(line, fields, problem)) return fail(problem);
        if (fields.size() != 3)
            return fail(QStringLiteral("expected 3 fields before #, found %1").arg(fields.size()));

        Category category{fields.at(0), fields.at(1), fields.at(2)};
        if (!loaded.add(category, &problem)) return fail(problem);
    }

    categories_ = loaded.categories_;
    if (error) error->clear();
    return true;
}

bool CategoryCatalog::save(QString* error) const
{
    if (filePath_.isEmpty()) {
        setError(error, QStringLiteral("load() must be called before save()"));
        return false;
    }

    QString text;
    for (const Category& c : categories_) {
        const QStringList fields = {
            quoteField(c.id, false),
            quoteField(c.name, true),
            quoteField(c.acronym, false),
            QStringLiteral("#"),
        };
        text += fields.join(QLatin1Char(' '));
        text += QLatin1Char('\n');
    }

    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("Cannot write %1: %2").arg(filePath_, file.errorString()));
        return false;
    }
    const QByteArray data = text.toUtf8();
    if (file.write(data) != data.size() || !file.commit()) {
        setError(error, QStringLiteral("Cannot write %1: %2").arg(filePath_, file.errorString()));
        return false;
    }
    if (error) error->clear();
    return true;
}

QVector<Category> CategoryCatalog::categories() const
{
    return categories_;
}

bool CategoryCatalog::findById(const QString& id, Category& category) const
{
    for (const Category& c : categories_) {
        if (c.id == id) {
            category = c;
            return true;
        }
    }
    return false;
}

bool CategoryCatalog::add(const Category& category, QString* error)
{
    QString problem;
    if (!checkCategory(category, problem)) {
        setError(error, problem);
        return false;
    }
    Category existing;
    if (findById(category.id, existing)) {
        setError(error, QStringLiteral("category ID %1 is already used by \"%2\"")
                            .arg(category.id, existing.name));
        return false;
    }
    categories_.append(category);
    if (error) error->clear();
    return true;
}

QString CategoryCatalog::nextFreeId() const
{
    for (int number = 1; number <= 99; ++number) {
        const QString id = QStringLiteral("%1").arg(number, 2, 10, QLatin1Char('0'));
        Category existing;
        if (!findById(id, existing)) return id;
    }
    return QString();
}

}
