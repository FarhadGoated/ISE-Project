#include "BarcodeFileIO.h"

#include <QFileInfo>
#include <QImageReader>
#include <QIODevice>
#include <QSaveFile>

namespace shop {

    bool BarcodeFileIO::savePng(
        const QImage& image,
        const QString& path,
        QString* error)
    {
        if (error != nullptr)
            error->clear();

        if (image.isNull()) {
            if (error != nullptr)
                *error = "Cannot save a null image.";

            return false;
        }

        if (path.isEmpty()) {
            if (error != nullptr)
                *error = "The file path is empty.";

            return false;
        }

        QFileInfo fileInfo(path);

        if (fileInfo.suffix().compare(
            QStringLiteral("png"),
            Qt::CaseInsensitive) != 0) {

            if (error != nullptr)
                *error = "The file must have a .png extension.";

            return false;
        }

        QSaveFile file(path);

        if (!file.open(QIODevice::WriteOnly)) {
            if (error != nullptr)
                *error = file.errorString();

            return false;
        }

        if (!image.save(&file, "PNG")) {
            file.cancelWriting();

            if (error != nullptr)
                *error = "PNG encoding failed.";

            return false;
        }

        if (!file.commit()) {
            if (error != nullptr)
                *error = file.errorString();

            return false;
        }

        return true;
    }


    QImage BarcodeFileIO::loadImage(
        const QString& path,
        QString* error)
    {
        if (error != nullptr)
            error->clear();

        if (path.isEmpty()) {
            if (error != nullptr)
                *error = "The file path is empty.";

            return QImage();
        }

        QFileInfo fileInfo(path);

        if (fileInfo.suffix().compare(
            QStringLiteral("png"),
            Qt::CaseInsensitive) != 0) {

            if (error != nullptr)
                *error = "The selected file must be a .png image.";

            return QImage();
        }

        QImageReader reader(path, "png");

        reader.setAutoDetectImageFormat(false);

        QImage image = reader.read();

        if (image.isNull()) {
            if (error != nullptr)
                *error = reader.errorString();

            return QImage();
        }

        return image;
    }

}