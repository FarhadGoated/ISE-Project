#include "BillPdf.h"

namespace shop {

bool exportBillPdf(const Bill&, const QString&, QString& error)
{
    // TODO(Shaffay): Format the receipt PDF from the final bill model.
    error = QStringLiteral("PDF export is pending.");
    return false;
}

} // namespace shop
