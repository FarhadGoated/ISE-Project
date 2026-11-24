#include "SeasonDiscounts.h"

namespace shop {

bool loadSeasons(const QString&, QVector<SeasonDiscount>&, QString& error)
{
    // TODO(Shaffay): Parse and validate Seasons.txt.
    error = QStringLiteral("Season loading is pending.");
    return false;
}

bool saveSeasons(const QString&, const QVector<SeasonDiscount>&, QString& error)
{
    // TODO(Shaffay): Save seasonal discounts, including their active flags.
    error = QStringLiteral("Season saving is pending.");
    return false;
}

int highestSeasonRate(const QVector<SeasonDiscount>&, const QDate&)
{
    // TODO(Shaffay): Use the highest eligible active rate with inclusive dates.
    return 0;
}

} // namespace shop
