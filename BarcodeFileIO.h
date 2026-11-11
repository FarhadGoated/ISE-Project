#include <QImage>
#include <QString>

namespace shop {

    class BarcodeFileIO {
    public:
        static bool savePng(
            const QImage& image,
            const QString& path,
            QString* error = nullptr
        );

        static QImage loadImage(
            const QString& path,
            QString* error = nullptr
        );
    };

}