#include "ScanDialog.h"

#include "BarcodeReader.h"
#include "CameraCapture.h"
#include "ImageProcessor.h"

#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableWidget>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
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
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath viewport;
        viewport.addRoundedRect(QRectF(rect()), 10, 10);
        painter.setClipPath(viewport);
        painter.fillRect(rect(), palette().color(QPalette::Window).darker(130));
        if (frame_.isNull()) {
            painter.setPen(palette().color(QPalette::WindowText));
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
        const QColor accent = accepted_ ? QColor("#8fe0a1") : palette().color(QPalette::Accent);
        painter.setPen(QPen(accent, 2));
        painter.drawRoundedRect(QRectF(guide).adjusted(1, 1, -1, -1), 6, 6);
        QColor centerLine = accent;
        centerLine.setAlpha(130);
        painter.setPen(QPen(centerLine, 1, Qt::DashLine));
        painter.drawLine(guide.left() + 9, guide.center().y(), guide.right() - 9, guide.center().y());
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

QString runScanner(QWidget* parent, const std::function<QString(QWidget*, const QString&)>& addBarcode,
                   const std::function<QList<ScanCartLine>()>& cartLines = {})
{
    QDialog dialog(parent);
    const QString title = addBarcode ? QStringLiteral("Scan Product") : QStringLiteral("Scan Barcode");
    dialog.setWindowTitle(title);
    dialog.setObjectName(QStringLiteral("scannerDialog"));
    dialog.resize(cartLines ? 1000 : 710, 500);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);
    auto* heading = new QLabel(title, &dialog);
    heading->setObjectName(QStringLiteral("scanTitle"));
    auto* instruction = new QLabel(
        QStringLiteral("Fit the full barcode and its white margins inside the frame."),
        &dialog);
    instruction->setObjectName(QStringLiteral("scanHint"));
    instruction->setWordWrap(true);
    auto* preview = new ScanPreview(&dialog);
    auto* footer = new QFrame(&dialog);
    footer->setObjectName(QStringLiteral("scanFooter"));
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(12, 8, 12, 8);
    auto* feedback = new QVBoxLayout;
    feedback->setSpacing(3);
    auto* status = new QLabel(QStringLiteral("Ready to scan"), footer);
    status->setObjectName(QStringLiteral("scanResult"));
    auto* statusDetail = new QLabel(footer);
    statusDetail->setObjectName(QStringLiteral("scanHint"));
    statusDetail->setMinimumHeight(statusDetail->fontMetrics().height());
    feedback->addWidget(status);
    feedback->addWidget(statusDetail);
    auto* close = new QPushButton(cartLines ? QStringLiteral("Back to Cart") : QStringLiteral("Close"), footer);
    close->setAutoDefault(false);
    statusDetail->setTextFormat(Qt::PlainText);
    statusDetail->setWordWrap(true);
    close->setObjectName(QStringLiteral("closeScanner"));
    footerLayout->addLayout(feedback, 1);
    footerLayout->addWidget(close);
    layout->addWidget(heading);
    layout->addWidget(instruction);
    auto* body = new QHBoxLayout;
    body->setSpacing(14);
    body->addWidget(preview, 1);
    QTableWidget* cartTable = nullptr;
    QLabel* cartHeading = nullptr;
    QLabel* emptyCart = nullptr;
    if (cartLines) {
        auto* panel = new QFrame(&dialog);
        panel->setProperty("panel", true);
        panel->setMinimumWidth(265);
        auto* side = new QVBoxLayout(panel);
        side->setContentsMargins(12, 10, 12, 12);
        cartHeading = new QLabel(panel);
        cartHeading->setProperty("heading", true);
        side->addWidget(cartHeading);
        cartTable = new QTableWidget(0, 2, panel);
        cartTable->setHorizontalHeaderLabels({QStringLiteral("Product"), QStringLiteral("Quantity")});
        cartTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        cartTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        cartTable->verticalHeader()->hide();
        cartTable->verticalHeader()->setDefaultSectionSize(34);
        cartTable->setSelectionMode(QAbstractItemView::NoSelection);
        cartTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        cartTable->setAlternatingRowColors(true);
        cartTable->setShowGrid(false);
        side->addWidget(cartTable, 1);
        emptyCart = new QLabel(QStringLiteral("Scan a product to start the cart."), panel);
        emptyCart->setProperty("muted", true);
        emptyCart->setWordWrap(true);
        side->addWidget(emptyCart);
        body->addWidget(panel);
    }
    const auto refreshCart = [&] {
        if (!cartTable) return;
        const auto lines = cartLines();
        cartHeading->setText(QStringLiteral("Current Cart (%1)").arg(lines.size()));
        emptyCart->setVisible(lines.isEmpty());
        cartTable->setRowCount(lines.size());
        for (qsizetype index = 0; index < lines.size(); ++index) {
            const auto& line = lines.at(index);
            auto* product = new QTableWidgetItem(line.product);
            product->setToolTip(line.product);
            cartTable->setItem(index, 0, product);
            auto* quantity = new QTableWidgetItem(line.quantity);
            quantity->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            cartTable->setItem(index, 1, quantity);
        }
    };
    refreshCart();
    layout->addLayout(body, 1);
    layout->addWidget(footer);

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

    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
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
        const QString addedProduct = addBarcode(&dialog, barcode);
        const bool added = !addedProduct.isEmpty();
        if (added) {
            preview->setAccepted(true);
            flash.start(330);
            refreshCart();
        }
        status->setText(QStringLiteral("Last scan  %1").arg(barcode));
        statusDetail->setText(added ? QStringLiteral("Added %1").arg(addedProduct)
                                   : QStringLiteral("Product was not added"));
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
    runScanner(parent, [&](QWidget* scanner, const QString& digits) {
        return addBarcode(scanner,digits) ? digits : QString();
    });
}

void scanOrderProducts(QWidget* parent, const std::function<QString(QWidget*, const QString&)>& addProduct,
                       const std::function<QList<ScanCartLine>()>& cartLines)
{
    runScanner(parent, addProduct, cartLines);
}

} // namespace shop
