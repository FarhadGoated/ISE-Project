#include "CameraCapture.h"

#include <QCameraDevice>
#include <QMediaDevices>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>

namespace shop {

CameraCapture::CameraCapture(QObject* parent)
    : QObject(parent)
{
    connect(&camera_, &QCamera::errorOccurred, this,
            [this](QCamera::Error, const QString& message) {
                emit cameraError(message);
            });
}

CameraCapture::~CameraCapture()
{
    stop();
}

void CameraCapture::start(QVideoWidget* preview)
{
    stop();

    const QCameraDevice device = QMediaDevices::defaultVideoInput();
    if (!preview || device.isNull()) {
        emit cameraError(QStringLiteral("No camera preview is available."));
        return;
    }

    camera_.setCameraDevice(device);
    session_.setCamera(&camera_);
    session_.setVideoOutput(preview);

    frameConnection_ = connect(
        preview->videoSink(), &QVideoSink::videoFrameChanged, this,
        [this](const QVideoFrame& frame) {
            constexpr int scanIntervalMs = 150;

            if (!camera_.isActive() || !frame.isValid()) {
                return;
            }

            if (frameTimer_.isValid()
                && frameTimer_.elapsed() < scanIntervalMs) {
                return;
            }

            frameTimer_.start();
            const QImage image = frame.toImage();

            if (!image.isNull()) {
                emit frameReady(image);
            }
        });

    camera_.start();
}

void CameraCapture::stop()
{
    disconnect(frameConnection_);
    frameConnection_ = {};
    camera_.stop();
    session_.setVideoOutput(nullptr);
    session_.setCamera(nullptr);
    frameTimer_.invalidate();
}

} // namespace shop