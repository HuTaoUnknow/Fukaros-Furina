#include <QApplication>
#include <QElapsedTimer>
#include <cstdio>
#include "windows/nvenc_encoder.h"
#include "windows/h264_decoder.h"
using namespace furina::video::detail;

// Manual GPU encoder / software decoder test. No display or user input changes.
static bool software(Decoder& decoder, int threads) {
    auto& a = api();
    decoder.clear();
    decoder.hardware = false;
    decoder.codec = a.avcodec_alloc_context3(a.avcodec_find_decoder_by_name("h264"));
    if (!decoder.codec)
        return false;
    decoder.codec->thread_count = 2;
    decoder.codec->thread_type = threads;
    decoder.codec->flags |= AV_CODEC_FLAG_LOW_DELAY;
    if (a.avcodec_open2(decoder.codec, decoder.codec->codec, nullptr) < 0)
        return false;
    decoder.frame = a.av_frame_alloc();
    decoder.cpu = a.av_frame_alloc();
    decoder.packet = a.av_packet_alloc();
    return true;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Com<IDXGIFactory1> factory;
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)factory.put())))
        return 3;
    for (UINT i = 0;; ++i) {
        Com<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, adapter.put()) == DXGI_ERROR_NOT_FOUND)
            break;
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.VendorId == 0x10de) {
            D3D_FEATURE_LEVEL level;
            if (SUCCEEDED(D3D11CreateDevice(adapter.p, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                            device.put(), &level, context.put())))
                break;
        }
    }
    if (!device.p)
        return 77;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 1200;
    desc.Height = 1920;
    desc.MipLevels = desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    Com<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, texture.put())))
        return 3;
    NvEncoder encoder;
    if (!encoder.open(device.p, texture.p, 1200, 1920))
        return 3;
    std::vector<QByteArray> packets;
    for (int i = 0; i < 12; ++i) {
        QImage frame(1200, 1920, QImage::Format_RGB32);
        frame.fill(QColor(40 + i * 10, 80, 120));
        context->UpdateSubresource(texture.p, 0, nullptr, frame.constBits(), frame.bytesPerLine(), 0);
        context->Flush();
        QByteArray packet;
        bool key = false;
        if (!encoder.encode(i == 0, &packet, &key))
            return 3;
        packets.push_back(packet);
    }
    bool valid = true;
    for (int mode = 0; mode < 2; ++mode) {
        Decoder decoder;
        if (!software(decoder, mode ? FF_THREAD_SLICE : FF_THREAD_FRAME | FF_THREAD_SLICE))
            return 3;
        int first = -1, frames = 0;
        QElapsedTimer clock;
        clock.start();
        for (size_t i = 0; i < packets.size(); ++i) {
            QImage image;
            if (!decoder.decode(packets[i], &image))
                return 2;
            if (!image.isNull()) {
                if (first < 0)
                    first = int(i);
                ++frames;
            }
        }
        printf(
            "software mode=%s first_output_at_input=%d input=%u output=%d average_work_ms=%.2f active_thread_type=%d\n",
            mode ? "slice" : "previous-auto", first, unsigned(packets.size()), frames,
            clock.nsecsElapsed() / 1000000.0 / packets.size(), decoder.codec->active_thread_type);
        fflush(stdout);
        if (mode)
            valid = first == 0 && frames == int(packets.size());
    }
    return valid ? 0 : 2;
}
