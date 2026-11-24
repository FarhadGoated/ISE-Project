#include "ScanDialog.h"

#include "BarcodeReader.h"
#include "CameraCapture.h"
#include "ImageProcessor.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QScopedValueRollback>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoSink>
#include <QtMath>

namespace shop {
namespace {

class ScanPreview final : public QWidget {
public:
    explicit ScanPreview(QWidget* parent) : QWidget(parent)
    {
        setMinimumSize(480, 270);
    }

    void setFrame(const QImage& frame)
    {
        frame_ = frame;
        update();
    }

    void setAccepted(bool accepted)
    {
        accepted_ = accepted;
        update();
    }

    QRect scanRegion() const
    {
        return mapScanGuideToFrame(guideRect(), imageRect(), frame_.size());
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#111318"));
        if (frame_.isNull()) {
            painter.setPen(Qt::white);
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Starting camera..."));
            return;
        }

        painter.drawImage(imageRect(), frame_);
        const QRect guide = guideRect();
        QPainterPath shade;
        shade.setFillRule(Qt::OddEvenFill);
        shade.addRect(QRectF(imageRect()));
        shade.addRect(QRectF(guide));
        painter.fillPath(shade, QColor(0, 0, 0, 145));
        painter.setPen(QPen(accepted_ ? QColor("#65e4ac") : Qt::white, 2));
        painter.drawRect(QRectF(guide).adjusted(1, 1, -1, -1));
    }

private:
    QRect imageRect() const
    {
        const QSize imageSize = frame_.size().scaled(size(), Qt::KeepAspectRatio);
        QRect image(QPoint(), imageSize);
        image.moveCenter(rect().center());
        return image;
    }

    QRect guideRect() const
    {
        const QRect image = imageRect();
        const int insetX = image.width() / 12;
        const int insetY = image.height() / 3;
        return image.adjusted(insetX, insetY, -insetX, -insetY);
    }

    QImage frame_;
    bool accepted_ = false;
};

QString decodeFrame(const QImage& frame, const QRect& region)
{
    for (const auto& runs : ImageProcessor::extractCameraRuns(frame, region)) {
        QString barcode;
        if (BarcodeReader::decodeCode39Digits(runs, barcode)) {
            return barcode;
        }
    }
    return {};
}

QString runScanner(QWidget* parent, const std::function<bool(QWidget*, const QString&)>& addBarcode)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Scan Barcode"));
    dialog.resize(660, 440);
    auto* layout = new QVBoxLayout(&dialog);
    auto* instruction = new QLabel(
        QStringLiteral("Fit the entire barcode, including both white margins, inside the box."),
        &dialog);
    instruction->setWordWrap(true);
    auto* preview = new ScanPreview(&dialog);
    auto* status = new QLabel(QStringLiteral("Ready to scan"), &dialog);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(instruction);
    layout->addWidget(preview, 1);
    layout->addWidget(status);
    layout->addWidget(buttons);

    QVideoSink sink;
    CameraCapture camera;
    QTimer flash;
    flash.setSingleShot(true);
    QString result;
    QString cameraError;
    QString lastSeen;
    int clearFrames = 0;
    bool processing = false;
    bool running = true;

    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(&dialog, &QDialog::finished, &camera, [&](int) { running = false; });
    QObject::connect(&flash, &QTimer::timeout, &dialog, [preview] {
        preview->setAccepted(false);
    });
    QObject::connect(&camera, &CameraCapture::cameraError, &dialog, [&](const QString& message) {
        if (running) {
            cameraError = message;
            dialog.reject();
        }
    });
    QObject::connect(&camera, &CameraCapture::frameReady, &dialog, [&](const QImage& frame) {
        if (!running) {
            return;
        }
        preview->setFrame(frame);
        if (processing) {
            return;
        }

        const QString barcode = decodeFrame(frame, preview->scanRegion());
        if (barcode.isEmpty()) {
            if (!lastSeen.isEmpty() && ++clearFrames >= 3) {
                lastSeen.clear();
                status->setText(QStringLiteral("Ready to scan"));
            }
            return;
        }
        clearFrames = 0;
        if (barcode == lastSeen) {
            return;
        }
        lastSeen = barcode;
        if (!addBarcode) {
            result = barcode;
            dialog.accept();
            return;
        }

        QScopedValueRollback<bool> guard(processing, true);
        if (addBarcode(&dialog, barcode)) {
            preview->setAccepted(true);
            flash.start(330);
            status->setText(QStringLiteral("Added barcode %1").arg(barcode));
        } else {
            status->setText(QStringLiteral("Barcode %1 was not added").arg(barcode));
        }
    });
    QTimer::singleShot(0, &dialog, [&] {
        if (running) {
            camera.start(sink);
        }
    });
    dialog.exec();
    camera.stop();
    if (!cameraError.isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("Camera"), cameraError);
    }
    return result;
}

} // namespace

QRect mapScanGuideToFrame(const QRect& guide, const QRect& displayedImage,
                         const QSize& frameSize)
{
    const QRect clipped = guide.intersected(displayedImage);
    if (clipped.isEmpty() || frameSize.isEmpty()) {
        return {};
    }
    const double scaleX = double(frameSize.width()) / displayedImage.width();
    const double scaleY = double(frameSize.height()) / displayedImage.height();
    const int left = clipped.left() - displayedImage.left();
    const int top = clipped.top() - displayedImage.top();
    const int x = qCeil(left * scaleX);
    const int y = qCeil(top * scaleY);
    const int right = qFloor((left + clipped.width()) * scaleX);
    const int bottom = qFloor((top + clipped.height()) * scaleY);
    if (right <= x || bottom <= y) {
        return {};
    }
    return QRect(x, y, right - x, bottom - y);
}

QString scanProductBarcode(QWidget* parent)
{
    return runScanner(parent, {});
}

void scanOrderBarcodes(QWidget* parent, const std::function<bool(QWidget*, const QString&)>& addBarcode)
{
    runScanner(parent, addBarcode);
}

} // namespace shop
