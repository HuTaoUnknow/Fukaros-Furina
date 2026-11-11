#pragma once
#include <QImage>
#include <QString>
#include <QStringList>

// Screen pixels per module. Version 8 at scale 6 is small enough that a camera's
// smooth sampling misses some sizes even when a nearby size decodes.
inline constexpr int kPairingQrScale = 8;

QStringList pairingLocalIpv4();
QImage pairingQrImage(const QString& text, int scale = kPairingQrScale);
QStringList pairingReadQrImage(const QImage& image);

// Clockwise degrees that turn a camera buffer upright on the current display.
// displayDegrees is 0, 90, 180, or 270. Back cameras use sensor - display.
int pairingCameraUprightDegrees(int sensorOrientation, int displayDegrees, bool frontFacing);

// Y plane of a camera frame. The last row may omit padding, and pixelStride may be greater than 1.
QImage pairingExtractLuma(const uchar* data, int length, int width, int height, int rowStride, int pixelStride);

// Clockwise rotation by 0, 90, 180, or 270 degrees.
QImage pairingRotateClockwise(const QImage& image, int degrees);
