#pragma once

#include <QCamera>
#include <QElapsedTimer>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMetaObject>
#include <QObject>
#include <QString>

class QVideoWidget;

namespace shop {

class CameraCapture : public QObject {
    Q_OBJECT

public:
    explicit CameraCapture(QObject* parent = nullptr);
    ~CameraCapture() override;

    void start(QVideoWidget* preview);
    void stop();

signals:
    void frameReady(const QImage& frame);
    void cameraError(const QString& message);

private:
    QCamera camera_;
    QMediaCaptureSession session_;
    QElapsedTimer frameTimer_;
    QMetaObject::Connection frameConnection_;
};

} // namespace shop