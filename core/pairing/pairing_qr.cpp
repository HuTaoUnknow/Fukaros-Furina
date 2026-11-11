#include "pairing_qr.h"
#include "qrcodegen.hpp"
#include "quirc.h"
#include <QNetworkInterface>
#include <cstring>
#include <memory>

QStringList pairingLocalIpv4() {
    QStringList hosts;
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp))
            continue;
        for (const auto& entry : iface.addressEntries()) {
            const auto address = entry.ip();
            if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback())
                hosts << address.toString();
        }
    }
    hosts.removeDuplicates();
    return hosts;
}

QImage pairingQrImage(const QString& text, int scale) {
    if (text.isEmpty() || scale < 1)
        return {};
    try {
        const auto qr = qrcodegen::QrCode::encodeText(text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int modules = qr.getSize();
        constexpr int border = 4;
        const int pixels = (modules + border * 2) * scale;
        QImage image(pixels, pixels, QImage::Format_RGB32);
        image.fill(Qt::white);
        for (int y = 0; y < modules; ++y) {
            for (int x = 0; x < modules; ++x) {
                if (!qr.getModule(x, y))
                    continue;
                for (int dy = 0; dy < scale; ++dy) {
                    auto* line = reinterpret_cast<QRgb*>(image.scanLine((y + border) * scale + dy));
                    for (int dx = 0; dx < scale; ++dx)
                        line[(x + border) * scale + dx] = qRgb(0, 0, 0);
                }
            }
        }
        return image;
    } catch (const std::exception&) {
        return {};
    }
}

namespace {
QStringList readGrayscale(const QImage& gray) {
    if (gray.isNull() || gray.width() < 21 || gray.height() < 21)
        return {};
    std::unique_ptr<quirc, decltype(&quirc_destroy)> decoder(quirc_new(), &quirc_destroy);
    if (!decoder || quirc_resize(decoder.get(), gray.width(), gray.height()) != 0)
        return {};
    int width = 0, height = 0;
    auto* buffer = quirc_begin(decoder.get(), &width, &height);
    for (int y = 0; y < height; ++y)
        std::memcpy(buffer + y * width, gray.constScanLine(y), static_cast<size_t>(width));
    quirc_end(decoder.get());
    QStringList found;
    const int count = quirc_count(decoder.get());
    for (int i = 0; i < count; ++i) {
        quirc_code code{};
        quirc_extract(decoder.get(), i, &code);
        quirc_data data{};
        if (quirc_decode(&code, &data) != QUIRC_SUCCESS || data.payload_len <= 0)
            continue;
        found << QString::fromUtf8(reinterpret_cast<const char*>(data.payload), data.payload_len);
    }
    return found;
}
} // namespace

QStringList pairingReadQrImage(const QImage& image) {
    if (image.isNull())
        return {};
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    auto found = readGrayscale(gray);
    if (!found.isEmpty())
        return found;
    // A camera's low-pass image can sit on a module phase quirc misses.
    // A second and third size usually land off that phase.
    for (const int percent : {75, 50}) {
        const int width = qMax(21, gray.width() * percent / 100);
        const int height = qMax(21, gray.height() * percent / 100);
        if (width == gray.width() && height == gray.height())
            continue;
        found = readGrayscale(gray.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        if (!found.isEmpty())
            return found;
    }
    return {};
}

int pairingCameraUprightDegrees(int sensorOrientation, int displayDegrees, bool frontFacing) {
    auto normalize = [](int value) {
        value %= 360;
        if (value < 0)
            value += 360;
        return value;
    };
    const int sensor = normalize(sensorOrientation);
    const int display = normalize(displayDegrees);
    return frontFacing ? normalize(sensor + display) : normalize(sensor - display);
}

QImage pairingExtractLuma(const uchar* data, int length, int width, int height, int rowStride, int pixelStride) {
    if (!data || width < 21 || height < 21 || pixelStride < 1 || length <= 0)
        return {};
    const qint64 rowBytes = static_cast<qint64>(width - 1) * pixelStride + 1;
    if (rowStride < rowBytes)
        return {};
    const qint64 needed = static_cast<qint64>(rowStride) * (height - 1) + rowBytes;
    if (needed > length)
        return {};
    QImage image(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y) {
        const uchar* row = data + static_cast<qint64>(y) * rowStride;
        uchar* dst = image.scanLine(y);
        if (pixelStride == 1) {
            std::memcpy(dst, row, static_cast<size_t>(width));
            continue;
        }
        for (int x = 0; x < width; ++x)
            dst[x] = row[static_cast<qint64>(x) * pixelStride];
    }
    return image;
}

QImage pairingRotateClockwise(const QImage& source, int degrees) {
    if (source.isNull())
        return {};
    degrees %= 360;
    if (degrees < 0)
        degrees += 360;
    if (degrees == 0)
        return source;
    const QImage image = source.convertToFormat(QImage::Format_Grayscale8);
    const int width = image.width();
    const int height = image.height();
    if (degrees == 180) {
        QImage turned(width, height, QImage::Format_Grayscale8);
        for (int y = 0; y < height; ++y) {
            const uchar* row = image.constScanLine(height - 1 - y);
            uchar* dst = turned.scanLine(y);
            for (int x = 0; x < width; ++x)
                dst[x] = row[width - 1 - x];
        }
        return turned;
    }
    if (degrees != 90 && degrees != 270)
        return image;
    QImage turned(height, width, QImage::Format_Grayscale8);
    if (degrees == 90) {
        for (int y = 0; y < height; ++y) {
            const uchar* row = image.constScanLine(y);
            for (int x = 0; x < width; ++x)
                turned.scanLine(x)[height - 1 - y] = row[x];
        }
        return turned;
    }
    for (int y = 0; y < height; ++y) {
        const uchar* row = image.constScanLine(y);
        for (int x = 0; x < width; ++x)
            turned.scanLine(width - 1 - x)[y] = row[x];
    }
    return turned;
}
