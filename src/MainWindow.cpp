#include "MainWindow.h"

#include "BarcodeFileIO.h"
#include "BarcodeGenerator.h"
#include "BarcodeReader.h"
#include "BarcodeTypes.h"
#include "ImageProcessor.h"
#include "InputValidator.h"

#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <vector>

namespace shop {

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
    }

}