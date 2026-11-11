#include "ImageProcessor.h"
#include <utility>

namespace shop {

    bool ImageProcessor::extractRuns(const QImage& image,
        std::vector<Run>& runs,
        QString* error) {
        runs.clear();
        if (error) error->clear();

        const QString failMsg = "No readable barcode on the center row.";

        if (image.isNull() || image.width() < 3) {
            if (error) *error = failMsg;
            return false;
        }

        const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
        if (gray.isNull()) {
            if (error) *error = "Could not convert image to grayscale.";
            return false;
        }

        std::vector<Run> out;
        const auto* row = gray.constScanLine(gray.height() / 2);
        bool isBlack = row[0] < 128;
        int widthPx = 1;
        for (int x = 1; x < gray.width(); ++x) {
            const bool nextIsBlack = row[x] < 128;
            if (nextIsBlack == isBlack) {
                ++widthPx;
            }
            else {
                out.push_back({ isBlack, widthPx });
                isBlack = nextIsBlack;
                widthPx = 1;
            }
        }
        out.push_back({ isBlack, widthPx });

        if (!out.empty() && !out.front().isBlack) out.erase(out.begin());
        if (!out.empty() && !out.back().isBlack) out.pop_back();

        if (out.size() < 3 || !out.front().isBlack || !out.back().isBlack) {
            if (error) *error = failMsg;
            return false;
        }

        runs = std::move(out);
        return true;
    }

} // namespace shop