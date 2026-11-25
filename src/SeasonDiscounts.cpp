#include "SeasonDiscounts.h"
#include "BillCalculator.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStringList>

namespace shop {

    namespace {

        QString seasonsPath(const QString& dataDir)
        {
            return QDir(dataDir).filePath(QStringLiteral("Seasons.txt"));
        }

        bool validateSeason(const SeasonDiscount& season, QString& error)
        {
            if (!season.startDate.isValid() || !season.endDate.isValid()) {
                error = QStringLiteral("Enter valid start and end dates (YYYY-MM-DD).");
                return false;
            }
            if (season.endDate < season.startDate) {
                error = QStringLiteral("The end date must not be before the start date.");
                return false;
            }
            if (season.basisPoints < 0 || season.basisPoints > 10000) {
                error = QStringLiteral("The discount must be between 0 and 100 percent.");
                return false;
            }
            return true;
        }

        bool parseStrictDate(const QString& token, QDate& date)
        {
            date = QDate::fromString(token, Qt::ISODate);
            return date.isValid() && date.toString(Qt::ISODate) == token;
        }

    }  // namespace

    bool loadSeasons(const QString& dataDir, QVector<SeasonDiscount>& seasons, QString& error)
    {
        error.clear();

        QFile file(seasonsPath(dataDir));
        if (!file.exists()) {
            seasons.clear();
            return true;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            error = QStringLiteral("Cannot open Seasons.txt: %1").arg(file.errorString());
            return false;
        }

        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        QVector<SeasonDiscount> loaded;

        for (int i = 0; i < lines.size(); ++i) {
            const QString line = lines[i].simplified();
            if (line.isEmpty()) {
                continue;
            }

            const QString where = QStringLiteral("Seasons.txt line %1: ").arg(i + 1);
            const QStringList tokens = line.split(QLatin1Char(' '));
            if (tokens.size() != 5 || tokens[4] != QLatin1String("#")) {
                error = where + QStringLiteral("expected start date, end date, percentage, true or false and #.");
                return false;
            }

            SeasonDiscount season;
            if (!parseStrictDate(tokens[0], season.startDate)
                || !parseStrictDate(tokens[1], season.endDate)) {
                error = where + QStringLiteral("dates must look like YYYY-MM-DD.");
                return false;
            }
            if (!BillCalculator::parsePercentBasisPoints(tokens[2], season.basisPoints)) {
                error = where + QStringLiteral("the discount percentage is not a number.");
                return false;
            }
            if (tokens[3] == QLatin1String("true")) {
                season.active = true;
            }
            else if (tokens[3] == QLatin1String("false")) {
                season.active = false;
            }
            else {
                error = where + QStringLiteral("the active flag must be true or false.");
                return false;
            }

            QString problem;
            if (!validateSeason(season, problem)) {
                error = where + problem;
                return false;
            }
            loaded.append(season);
        }

        seasons = loaded;
        return true;
    }

    bool saveSeasons(const QString& dataDir, const QVector<SeasonDiscount>& seasons, QString& error)
    {
        error.clear();
        if (dataDir.isEmpty()) {
            error = QStringLiteral("No data folder was given for Seasons.txt.");
            return false;
        }

        QString text;
        for (int i = 0; i < seasons.size(); ++i) {
            const SeasonDiscount& season = seasons[i];
            QString problem;
            if (!validateSeason(season, problem)) {
                error = QStringLiteral("Season %1: %2").arg(i + 1).arg(problem);
                return false;
            }
            text += season.startDate.toString(Qt::ISODate) + QLatin1Char(' ')
                + season.endDate.toString(Qt::ISODate) + QLatin1Char(' ')
                + BillCalculator::formatPercentBasisPoints(season.basisPoints) + QLatin1Char(' ')
                + (season.active ? QStringLiteral("true") : QStringLiteral("false"))
                + QStringLiteral(" #\n");
        }

        QDir().mkpath(dataDir);

        QSaveFile file(seasonsPath(dataDir));
        if (!file.open(QIODevice::WriteOnly)) {
            error = QStringLiteral("Cannot write Seasons.txt: %1").arg(file.errorString());
            return false;
        }
        if (file.write(text.toUtf8()) < 0) {
            file.cancelWriting();
            error = QStringLiteral("Cannot write Seasons.txt: %1").arg(file.errorString());
            return false;
        }
        if (!file.commit()) {
            error = QStringLiteral("Cannot save Seasons.txt: %1").arg(file.errorString());
            return false;
        }
        return true;
    }

    int highestSeasonRate(const QVector<SeasonDiscount>& seasons, const QDate& date)
    {
        int best = 0;
        for (const SeasonDiscount& season : seasons) {
            if (!season.active) {
                continue;
            }
            if (date < season.startDate || date > season.endDate) {
                continue;
            }
            if (season.basisPoints > best) {
                best = season.basisPoints;
            }
        }
        return best;
    }

} // namespace shop