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

QStringList pairingReadQrImage(const QImage& image) {
    if (image.isNull())
        return {};
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
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
