#pragma once
#include "backend_types.h"
#include "video_policy.h"

namespace furina::video::detail {
struct Decoder {
    AVCodecContext* codec = nullptr;
    AVFrame *frame = nullptr, *cpu = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* scaler = nullptr;
    QString error;
    bool hardware = false;
    AVBufferRef* device = nullptr;

    ~Decoder() {
        clear();
        if (device)
            api().av_buffer_unref(&device);
    }

    void clear() {
        auto& a = api();
        if (scaler)
            a.sws_freeContext(scaler);
        scaler = nullptr;
        if (frame)
            a.av_frame_free(&frame);
        if (cpu)
            a.av_frame_free(&cpu);
        if (packet)
            a.av_packet_free(&packet);
        if (codec)
            a.avcodec_free_context(&codec);
    }

    static AVPixelFormat format(AVCodecContext*, const AVPixelFormat* formats) {
        for (auto p = formats; *p != AV_PIX_FMT_NONE; ++p)
            if (*p == AV_PIX_FMT_D3D11)
                return *p;
        return formats[0];
    }

    bool open() {
        clear();
        auto& a = api();
        const auto* decoder = a.avcodec_find_decoder_by_name("h264");
        if (!decoder) {
            error = "H.264 decoder unavailable";
            return false;
        }
        codec = a.avcodec_alloc_context3(decoder);
        codec->thread_count = 2;
        codec->thread_type = FF_THREAD_SLICE;
        codec->flags |= AV_CODEC_FLAG_LOW_DELAY;
        if (!device)
            a.av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
        hardware = device != nullptr;
        if (hardware) {
            codec->hw_device_ctx = a.av_buffer_ref(device);
            codec->get_format = &format;
        }
        const int result = a.avcodec_open2(codec, decoder, nullptr);
        if (result < 0) {
            error = a.error(result);
            clear();
            return false;
        }
        frame = a.av_frame_alloc();
        cpu = a.av_frame_alloc();
        packet = a.av_packet_alloc();
        return true;
    }

    bool decode(const QByteArray& bytes, QImage* out) {
        auto& a = api();
        a.av_packet_unref(packet);
        if (a.av_new_packet(packet, bytes.size()) < 0)
            return false;
        memcpy(packet->data, bytes.constData(), bytes.size());
        const int sent = a.avcodec_send_packet(codec, packet);
        if (sent < 0 && sent != AVERROR(EAGAIN)) {
            error = a.error(sent);
            return false;
        }
        int result;
        while ((result = a.avcodec_receive_frame(codec, frame)) >= 0) {
            AVFrame* image = frame;
            if (frame->format == AV_PIX_FMT_D3D11) {
                a.av_frame_unref(cpu);
                if (a.av_hwframe_transfer_data(cpu, frame, 0) < 0) {
                    a.av_frame_unref(frame);
                    continue;
                }
                image = cpu;
            }
            if (image->width > 4096 || image->height > 4096 || image->width < 2 || image->height < 2) {
                a.av_frame_unref(frame);
                return false;
            }
            scaler = a.sws_getCachedContext(scaler, image->width, image->height, AVPixelFormat(image->format),
                                            image->width, image->height, AV_PIX_FMT_BGRA, SWS_FAST_BILINEAR,
                                            nullptr, nullptr, nullptr);
            if (!scaler) {
                a.av_frame_unref(frame);
                return false;
            }
            // swscale does not automatically use AVFrame color metadata. Read it from
            // the decoded frame (hardware transfer only copies the pixel planes).
            const int matrix =
                frame->colorspace == AVCOL_SPC_BT709
                    ? SWS_CS_ITU709
                    : (frame->colorspace == AVCOL_SPC_BT2020_NCL ? SWS_CS_BT2020 : SWS_CS_DEFAULT);
            const int* coefficients = a.sws_getCoefficients(matrix);
            a.sws_setColorspaceDetails(scaler, coefficients, frame->color_range == AVCOL_RANGE_JPEG,
                                       coefficients, 1, 0, 1 << 16, 1 << 16);
            QImage rgb(image->width, image->height, QImage::Format_RGB32);
            uint8_t* dst[] = {rgb.bits(), nullptr, nullptr, nullptr};
            int stride[] = {int(rgb.bytesPerLine()), 0, 0, 0};
            a.sws_scale(scaler, image->data, image->linesize, 0, image->height, dst, stride);
            *out = rgb;
            a.av_frame_unref(frame);
        }
        return result == AVERROR(EAGAIN) || result == AVERROR_EOF;
    }
};
} // namespace furina::video::detail
