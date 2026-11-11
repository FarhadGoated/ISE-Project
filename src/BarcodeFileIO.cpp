#include "BarcodeFileIO.h"

#include <QFileInfo>
#include <QImageReader>
#include <QIODevice>
#include <QSaveFile>

namespace shop {

    bool BarcodeFileIO::savePng(const QImage& image, const QString& path, QString* error)
    {
        if (error) {
            error->clear();
        }

        if (image.isNull()) {
            if (error) {
                *error = QStringLiteral("There is no barcode image to save.");
            }
            return false;
        }

        if (path.isEmpty()
            || QFileInfo(path).suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) != 0) {
            if (error) {
                *error = QStringLiteral("Choose a file name ending in .png.");
            }
            return false;
        }

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error) {
                *error = file.errorString();
            }
            return false;
        }

        if (!image.save(&file, "PNG")) {
            file.cancelWriting();
            if (error) {
                *error = QStringLiteral("Could not encode the barcode as PNG.");
            }
            return false;
        }

        if (!file.commit()) {
            if (error) {
                *error = file.errorString();
            }
            return false;
        }

        return true;
    }

    QImage BarcodeFileIO::loadImage(const QString& path, QString* error)
    {
        if (error) {
            error->clear();
        }

        if (path.isEmpty()
            || QFileInfo(path).suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) != 0) {
            if (error) {
                *error = QStringLiteral("Choose a PNG image.");
            }
            return {};
        }

        QImageReader reader(path, "png");
        reader.setAutoDetectImageFormat(false);

        const QImage image = reader.read();
        if (image.isNull() && error) {
            *error = reader.errorString();
        }

        return image;
    }

}