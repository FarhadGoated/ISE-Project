#include "ScanDialog.h"

namespace shop {

QString scanProductBarcode(QWidget*)
{
    // TODO(Farhad): Reuse the live camera, draw the guide, and deliver decoded digits.
    return {};
}

QRect mapScanGuideToFrame(const QRect&, const QRect&, const QSize&)
{
    // TODO(Farhad): Map the guide through preview scaling and letterboxing.
    return {};
}

} // namespace shop
