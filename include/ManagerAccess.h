#pragma once

#include <QString>

namespace shop {

bool verifyManagerPin(const QString& pin);
bool changeManagerPin(const QString& currentPin, const QString& newPin, QString& error);

} // namespace shop
