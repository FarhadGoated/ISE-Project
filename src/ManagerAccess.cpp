#include "ManagerAccess.h"

#include <QCryptographicHash>
#include <QSettings>

namespace shop {
namespace {

bool validPin(const QString& pin)
{
    if (pin.size() != 4) {
        return false;
    }
    for (const QChar digit : pin) {
        if (digit < QLatin1Char('0') || digit > QLatin1Char('9')) {
            return false;
        }
    }
    return true;
}

QByteArray digest(const QString& pin)
{
    return QCryptographicHash::hash(pin.toUtf8(), QCryptographicHash::Sha256);
}

} // namespace

bool verifyManagerPin(const QString& pin)
{
    if (!validPin(pin)) {
        return false;
    }
    QSettings settings(QStringLiteral("RandomKidz"), QStringLiteral("SuperDuperPOS"));
    return settings.value(QStringLiteral("manager/pinDigest"), digest(QStringLiteral("0000")))
        .toByteArray() == digest(pin);
}

bool changeManagerPin(const QString& currentPin, const QString& newPin, QString& error)
{
    error.clear();
    if (!verifyManagerPin(currentPin)) {
        error = QStringLiteral("The current PIN is incorrect.");
        return false;
    }
    if (!validPin(newPin)) {
        error = QStringLiteral("Enter exactly four digits for the new PIN.");
        return false;
    }
    QSettings settings(QStringLiteral("RandomKidz"), QStringLiteral("SuperDuperPOS"));
    settings.setValue(QStringLiteral("manager/pinDigest"), digest(newPin));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        error = QStringLiteral("Could not save the manager PIN.");
        return false;
    }
    return true;
}

} // namespace shop
