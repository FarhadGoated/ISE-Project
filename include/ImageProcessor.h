#pragma once
#include "BarcodeTypes.h"
#include <QImage>
#include <QString>
#include <vector>

namespace shop {
class ImageProcessor {
public:
    static bool extractRuns(const QImage& image, std::vector<Run>& runs,
                            QString* error = nullptr);
};
}