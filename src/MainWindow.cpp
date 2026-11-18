#include "MainWindow.h"

#include "BarcodeFileIO.h"
#include "BarcodeGenerator.h"
#include "BarcodeReader.h"
#include "BarcodeTypes.h"
#include "CameraCapture.h"
#include "ImageProcessor.h"
#include "InputValidator.h"

#include <QDialog>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWidget>

#include <vector>

namespace shop {

    namespace {

        QString scanBarcode(QWidget* parent)
        {
            QDialog dialog(parent);
            dialog.setWindowTitle(QStringLiteral("Scan barcode"));

            auto* layout = new QVBoxLayout(&dialog);
            auto* instructions = new QLabel(
                QStringLiteral("Center the barcode horizontally with both white margins visible."),
                &dialog);
            instructions->setWordWrap(true);

            auto* preview = new QVideoWidget(&dialog);
            preview->setMinimumSize(480, 270);

            auto* cancelButton = new QPushButton(QStringLiteral("Cancel"), &dialog);
            layout->addWidget(instructions);
            layout->addWidget(preview, 1);
            layout->addWidget(cancelButton);

            QString decoded;
            QString cameraError;
            bool scanning = true;
            CameraCapture capture;

            QObject::connect(cancelButton, &QPushButton::clicked,
                &dialog, &QDialog::reject);

            QObject::connect(&dialog, &QDialog::finished, &capture, [&](int) {
                scanning = false;
                });

            QObject::connect(&capture, &CameraCapture::cameraError, &dialog,
                [&](const QString& message) {
                    if (!scanning) {
                        return;
                    }

                    cameraError = message;
                    dialog.reject();
                });

            QObject::connect(&capture, &CameraCapture::frameReady, &dialog,
                [&](const QImage& frame) {
                    if (!scanning || frame.isNull()) {
                        return;
                    }

                    const int insetX = frame.width() / 20;
                    const int insetY = frame.height() / 4;
                    const QRect scanArea = frame.rect().adjusted(
                        insetX, insetY, -insetX, -insetY);

                    for (const auto& runs : ImageProcessor::extractCameraRuns(frame, scanArea)) {
                        if (BarcodeReader::decodeCode39Digits(runs, decoded)) {
                            dialog.accept();
                            return;
                        }
                    }
                });

            QTimer::singleShot(0, &dialog, [&] {
                if (scanning) {
                    capture.start(preview);
                }
                });

            const int result = dialog.exec();
            scanning = false;
            capture.stop();

            if (!cameraError.isEmpty()) {
                QMessageBox::warning(parent, QStringLiteral("Camera"), cameraError);
            }

            return result == QDialog::Accepted ? decoded : QString{};
        }

    } // namespace

    MainWindow::MainWindow(QWidget* parent)
        : QMainWindow(parent)
    {
        setWindowTitle(QStringLiteral("Barcode Generator and Reader"));
        resize(600, 440);

        auto* content = new QWidget(this);
        setCentralWidget(content);
        auto* layout = new QVBoxLayout(content);

        auto* inputLabel = new QLabel(QStringLiteral("Six-digit number"), content);
        auto* input = new QLineEdit(content);
        inputLabel->setBuddy(input);

        auto* generateButton = new QPushButton(QStringLiteral("Generate"), content);
        auto* saveButton = new QPushButton(QStringLiteral("Save PNG"), content);
        auto* readButton = new QPushButton(QStringLiteral("Read PNG"), content);
        auto* scanButton = new QPushButton(QStringLiteral("Scan Camera"), content);
        saveButton->setEnabled(false);

        auto* preview = new QLabel(QStringLiteral("Barcode preview"), content);
        preview->setAlignment(Qt::AlignCenter);
        preview->setMinimumSize(450, 160);

        auto* decodedLabel = new QLabel(QStringLiteral("Decoded number"), content);
        auto* decodedOutput = new QLineEdit(content);
        decodedOutput->setReadOnly(true);
        decodedLabel->setBuddy(decodedOutput);

        layout->addWidget(inputLabel);
        layout->addWidget(input);
        layout->addWidget(generateButton);
        layout->addWidget(saveButton);
        layout->addWidget(preview, 1);
        layout->addWidget(readButton);
        layout->addWidget(scanButton);
        layout->addWidget(decodedLabel);
        layout->addWidget(decodedOutput);

        const auto showBarcode = [this, preview, saveButton](const QImage& image) {
            previewImage_ = image;
            const QPixmap pixmap = QPixmap::fromImage(previewImage_);
            const QSize previewSize = preview->contentsRect().size();
            preview->setPixmap(pixmap.scaled(
                pixmap.size().boundedTo(previewSize), Qt::KeepAspectRatio));
            saveButton->setEnabled(true);
            };

        connect(generateButton, &QPushButton::clicked, this,
            [this, input, decodedOutput, showBarcode] {
                const QString digits = input->text();
                QString error;

                if (!InputValidator::isValidSixDigitNumber(digits, &error)) {
                    QMessageBox::warning(this, QStringLiteral("Input validation"), error);
                    return;
                }

                const QImage image = BarcodeGenerator::generateCode39(digits, &error);
                if (image.isNull()) {
                    QMessageBox::warning(this, QStringLiteral("Barcode generation"), error);
                    return;
                }

                decodedOutput->clear();
                showBarcode(image);
            });

        connect(saveButton, &QPushButton::clicked, this, [this] {
            QFileDialog dialog(this, QStringLiteral("Save barcode"));
            dialog.setAcceptMode(QFileDialog::AcceptSave);
            dialog.setNameFilter(QStringLiteral("PNG images (*.png)"));
            dialog.setDefaultSuffix(QStringLiteral("png"));
            dialog.selectFile(QStringLiteral("barcode.png"));

            if (dialog.exec() != QDialog::Accepted) {
                return;
            }

            const QString path = dialog.selectedFiles().constFirst();
            QString error;

            if (!BarcodeFileIO::savePng(previewImage_, path, &error)) {
                QMessageBox::warning(this, QStringLiteral("Save barcode"), error);
            }
            });

        connect(readButton, &QPushButton::clicked, this,
            [this, decodedOutput, showBarcode] {
                const QString path = QFileDialog::getOpenFileName(
                    this, QStringLiteral("Open barcode"), QString(),
                    QStringLiteral("PNG images (*.png)"));
                if (path.isEmpty()) {
                    return;
                }

                QString error;
                const QImage image = BarcodeFileIO::loadImage(path, &error);
                if (image.isNull()) {
                    QMessageBox::warning(this, QStringLiteral("Open barcode"), error);
                    return;
                }

                std::vector<Run> runs;
                if (!ImageProcessor::extractRuns(image, runs, &error)) {
                    QMessageBox::warning(this, QStringLiteral("Read barcode"), error);
                    return;
                }

                QString digits;
                if (!BarcodeReader::decodeCode39Digits(runs, digits, &error)) {
                    QMessageBox::warning(this, QStringLiteral("Read barcode"), error);
                    return;
                }

                showBarcode(image);
                decodedOutput->setText(digits);
            });

        connect(scanButton, &QPushButton::clicked, this, [this, decodedOutput] {
            const QString digits = scanBarcode(this);
            if (!digits.isEmpty()) {
                decodedOutput->setText(digits);
            }
            });
    }

} // namespace shop