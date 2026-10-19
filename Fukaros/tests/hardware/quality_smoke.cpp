#include <QApplication>
#include <QElapsedTimer>
#include <QPainter>
#include <cmath>
#include <cstdio>
#include "windows/nvenc_encoder.h"
#include "windows/h264_decoder.h"
using namespace furina::video::detail;

// Real NVENC round-trip with unchanged fine text and changing desktop-like detail.
// No screen capture, driver changes, network or user windows are required.
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Com<IDXGIFactory1> factory;
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)factory.put())))
        return 3;
    for (UINT i = 0;; ++i) {
        Com<IDXGIAdapter1> a;
        if (factory->EnumAdapters1(i, a.put()) == DXGI_ERROR_NOT_FOUND)
            break;
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        if (d.VendorId == 0x10de) {
            D3D_FEATURE_LEVEL level;
            if (SUCCEEDED(D3D11CreateDevice(a.p, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                            device.put(), &level, context.put())))
                break;
        }
    }
    if (!device.p) {
        fprintf(stderr, "NVIDIA D3D11 unavailable\n");
        return 3;
    }
    constexpr int w = 1920, h = 1200;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    Com<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, texture.put())))
        return 3;
    NvEncoder encoder;
    Decoder decoder;
    if (!encoder.open(device.p, texture.p, w, h) || !decoder.open()) {
        fprintf(stderr, "codec: %s %s\n", encoder.error.toUtf8().constData(),
                decoder.error.toUtf8().constData());
        return 3;
    }
    QImage reference(w, h, QImage::Format_RGB32);
    QPainter p(&reference);
    p.fillRect(reference.rect(), QColor(40, 45, 50));
    for (int y = 0; y < h; y += 12)
        for (int x = 0; x < w; x += 12) {
            unsigned n = (x * 1337u + y * 331u) ^ ((x + y) * 7331u);
            int v = 30 + int(n % 150);
            p.fillRect(x, y, 12, 12, QColor(v, v, v));
        }
    const QRect roi(100, 100, 700, 300);
    p.fillRect(roi, Qt::white);
    p.setPen(Qt::black);
    p.setFont(QFont("Consolas", 8));
    for (int y = 120; y < 330; y += 15)
        p.drawText(
            120, y,
            QString("Furina desktop quality 1920x1200 60 FPS / ABCDEFGHIJKLMNOP 0123456789 %1").arg(y));
    for (int x = 110; x < 780; x += 4)
        p.fillRect(x, 350, 2, 35, Qt::black);
    p.end();
    double keyMin = 100, otherSum = 0, otherMin = 100, encodeMs = 0;
    int keys = 0, others = 0, maxQp = 0, decoded = 0;
    unsigned long long totalBytes = 0;
    bool valid = true;
    for (int i = 0; i < 240; ++i) {
        QImage input = reference.copy();
        QPainter moving(&input);
        moving.fillRect(850 + (i * 13) % 700, 450, 220, 430,
                        QColor((i * 7) % 256, (i * 3) % 256, (i * 5) % 256));
        moving.end();
        context->UpdateSubresource(texture.p, 0, nullptr, input.constBits(), input.bytesPerLine(), 0);
        context->Flush();
        QByteArray bytes;
        bool key = false;
        QElapsedTimer elapsed;
        elapsed.start();
        if (!encoder.encode(i % 60 == 0, &bytes, &key)) {
            valid = false;
            break;
        }
        encodeMs += elapsed.nsecsElapsed() / 1000000.0;
        maxQp = qMax(maxQp, encoder.lastQp);
        totalBytes += bytes.size();
        QImage image;
        if (!decoder.decode(bytes, &image) || image.isNull()) {
            valid = false;
            break;
        }
        ++decoded;
        double error = 0;
        for (int y = roi.top(); y <= roi.bottom(); ++y) {
            const QRgb *a = (const QRgb*)reference.constScanLine(y), *b = (const QRgb*)image.constScanLine(y);
            for (int x = roi.left(); x <= roi.right(); ++x) {
                int delta = qGray(a[x]) - qGray(b[x]);
                error += delta * delta;
            }
        }
        double psnr = error > 0 ? 10 * std::log10(255.0 * 255.0 * roi.width() * roi.height() / error) : 100;
        if (key) {
            ++keys;
            keyMin = qMin(keyMin, psnr);
            printf("KEY frame=%d qp=%d bytes=%lld text_psnr=%.2f\n", i, encoder.lastQp,
                   (long long)bytes.size(), psnr);
        } else {
            ++others;
            otherSum += psnr;
            otherMin = qMin(otherMin, psnr);
        }
        if (i == 60 || i == 61)
            image.save(QString("quality-frame-%1.png").arg(i));
    }
    printf(
        "RESULT decoded=%d key_min_psnr=%.2f p_mean_psnr=%.2f p_min_psnr=%.2f max_qp=%d encode_ms=%.2f bitrate_mbps=%.2f\n",
        decoded, keyMin, otherSum / qMax(1, others), otherMin, maxQp, encodeMs / qMax(1, decoded),
        totalBytes * 8.0 / 4 / 1000000);
    fflush(stdout);
    valid = valid && decoded == 240 && keys >= 4 && keyMin > 32 && otherMin > 32 && maxQp <= 30 &&
            encodeMs / decoded < 16.67;
    encoder.clear();
    decoder.clear();
    return valid ? 0 : 2;
}
