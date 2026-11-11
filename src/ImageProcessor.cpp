#include "ImageProcessor.h"
#include <utility>
#include <algorithm>
#include <cstddef>

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
    std::vector<std::vector<Run>> ImageProcessor::extractCameraRuns(
        const QImage& frame, const QRect& scanArea)
    {
        constexpr std::size_t barcodeRunCount = 79;
        constexpr int minContrast = 40;

        const QRect area = frame.rect().intersected(scanArea);
        if (frame.isNull() || area.width() < 81 || area.height() < 5) {
            return {};
        }
        const QImage gray = frame.convertToFormat(QImage::Format_Grayscale8);
        if (gray.isNull()) {
            return {};
        }

        std::vector<std::vector<Run>> candidates;
        const int rowStep = std::max(1, area.height() / 10);

        for (int offset : {0, -1, 1, -2, 2}) {
            const int y = area.center().y() + offset * rowStep;
            const auto* first = gray.constScanLine(y) + area.left();
            const auto* last = first + area.width();

            const auto bounds = std::minmax_element(first, last);
            if (*bounds.second - *bounds.first < minContrast) {
                continue;
            }
            const int threshold = (*bounds.first + *bounds.second) / 2;

            std::vector<Run> rowRuns;
            for (const auto* pixel = first; pixel != last; ++pixel) {
                const bool black = *pixel < threshold;
                if (rowRuns.empty() || rowRuns.back().isBlack != black) {
                    rowRuns.push_back({ black, 1 });
                }
                else {
                    ++rowRuns.back().widthPx;
                }
            }

            for (std::size_t start = 1;
                start + barcodeRunCount < rowRuns.size(); ++start) {
                const std::size_t end = start + barcodeRunCount;
                if (!rowRuns[start].isBlack) {
                    continue;
                }
                const Run& left = rowRuns[start - 1];
                const Run& right = rowRuns[end];
                if (left.widthPx < 2LL * rowRuns[start].widthPx
                    || right.widthPx < 2LL * rowRuns[end - 1].widthPx) {
                    continue;
                }
                candidates.emplace_back(rowRuns.begin() + start,
                    rowRuns.begin() + end);
            }
        }
        return candidates;
    }

} // namespace shop