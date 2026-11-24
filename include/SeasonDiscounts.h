#pragma once

#include <QDate>
#include <QString>
#include <QVector>

namespace shop {

struct SeasonDiscount {
    QDate startDate;
    QDate endDate;
    int basisPoints = 0; // 100 basis points = 1%.
    bool active = false;
};

bool loadSeasons(const QString& dataDir, QVector<SeasonDiscount>& seasons, QString& error);
bool saveSeasons(const QString& dataDir, const QVector<SeasonDiscount>& seasons, QString& error);
int highestSeasonRate(const QVector<SeasonDiscount>& seasons, const QDate& date);

} // namespace shop
