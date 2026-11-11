#include "pairing_qr.h"
extern "C" {
#include "qrcodegen.h"
}

QImage furinaQrImage(const QString& text, int scale) {
    if (text.isEmpty() || scale < 2 || scale > 16)
        return {};
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(12)];
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(12)];
    const auto utf8 = text.toUtf8();
    if (!qrcodegen_encodeText(utf8.constData(), temp, qr, qrcodegen_Ecc_MEDIUM, 1, 12, qrcodegen_Mask_AUTO, true))
        return {};
    const int size = qrcodegen_getSize(qr);
    const int quiet = 4;
    const int dim = (size + quiet * 2) * scale;
    QImage image(dim, dim, QImage::Format_RGB32);
    image.fill(Qt::white);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (!qrcodegen_getModule(qr, x, y))
                continue;
            for (int yy = 0; yy < scale; ++yy)
                for (int xx = 0; xx < scale; ++xx)
                    image.setPixel((quiet + x) * scale + xx, (quiet + y) * scale + yy, qRgb(0, 0, 0));
        }
    }
    return image;
}
