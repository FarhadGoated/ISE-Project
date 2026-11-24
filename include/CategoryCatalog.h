#pragma once

#include <QString>
#include <QVector>

namespace shop {

struct Category {
    QString id;
    QString name;
    QString acronym;
};

bool loadCategories(const QString& dataDir, QVector<Category>& categories, QString& error);
bool saveCategories(const QString& dataDir, const QVector<Category>& categories, QString& error);

} // namespace shop
