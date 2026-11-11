#include "ImageProcessor.h"

namespace shop {
bool ImageProcessor::extractRuns(const QImage&, std::vector<Run>& runs,
                                 QString* error) {
    runs.clear();
    if (error) {
        *error = QStringLiteral("Image processing is not implemented yet.");
    }
    return false;
}
}