#include "ManagerAccess.h"

namespace shop {

bool verifyManagerPin(const QString&)
{
    // TODO(Farhad): Check the saved manager PIN. The initial PIN is 0000.
    return false;
}

bool changeManagerPin(const QString&, const QString&, QString& error)
{
    // TODO(Farhad): Verify the current PIN before persisting the replacement.
    error = QStringLiteral("Manager PIN changes are pending.");
    return false;
}

} // namespace shop
