#pragma once
#include "backend_types.h"
#include "video_policy.h"

namespace furina::video::detail {
inline QByteArray annexB(const uint8_t* data, int size) {
    if (!data || size <= 0)
        return {};
    if (size >= 4 && data[0] == 0 && data[1] == 0 && (data[2] == 1 || (data[2] == 0 && data[3] == 1)))
        return QByteArray(reinterpret_cast<const char*>(data), size);
    QByteArray out;
    int pos = 0;
    if (size >= 7 && data[0] == 1) {
        pos = 5;
        int count = data[pos++] & 31;
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < count; ++i) {
                if (pos + 2 > size)
                    return {};
                int n = (data[pos] << 8) | data[pos + 1];
                pos += 2;
                if (n <= 0 || pos + n > size)
                    return {};
                out.append("\0\0\0\1", 4);
                out.append(reinterpret_cast<const char*>(data + pos), n);
                pos += n;
            }
            if (pass == 0) {
                if (pos >= size)
                    return {};
                count = data[pos++];
            }
        }
    } else {
        while (pos + 4 <= size) {
            uint32_t n = fr_get_u32(data + pos);
            pos += 4;
            if (n == 0 || n > uint32_t(size - pos))
                return {};
            out.append("\0\0\0\1", 4);
            out.append(reinterpret_cast<const char*>(data + pos), n);
            pos += n;
        }
        if (pos != size)
            return {};
    }
    return out;
}

struct Encoder {
    AVCodecContext* codec = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* scaler = nullptr;
    int64_t sequence = 0;
    QString error;

    ~Encoder() {
        clear();
    }

    void clear() {
        auto& a = api();
        if (scaler)
            a.sws_freeContext(scaler);
        scaler = nullptr;
        if (frame)
            a.av_frame_free(&frame);
        if (packet)
            a.av_packet_free(&packet);
        if (codec)
            a.avcodec_free_context(&codec);
    }

    bool open(int w, int h) {
        clear();
        auto& a = api();
        const auto* encoder = a.avcodec_find_encoder_by_name("h264_mf");
        if (!encoder) {
            error = "h264_mf encoder unavailable";
            return false;
        }
        codec = a.avcodec_alloc_context3(encoder);
        codec->width = w;
        codec->height = h;
        codec->pix_fmt = AV_PIX_FMT_NV12;
        codec->time_base = {1, 60};
        codec->framerate = {60, 1};
        codec->gop_size = FR_VIDEO_GOP;
        codec->max_b_frames = 0;
        codec->flags |= AV_CODEC_FLAG_LOW_DELAY;
        codec->profile = AV_PROFILE_H264_BASELINE;
        codec->bit_rate = fr_video_bitrate(w, h);
        codec->rc_max_rate = codec->bit_rate * 3 / 2;
        codec->rc_buffer_size = int(codec->bit_rate / 5);
        codec->compression_level = 0;
        a.av_opt_set_int(codec->priv_data, "hw_encoding", 1, 0);
        a.av_opt_set(codec->priv_data, "scenario", "display_remoting", 0);
        a.av_opt_set(codec->priv_data, "rate_control", "ld_vbr", 0);
        const int result = a.avcodec_open2(codec, encoder, nullptr);
        if (result < 0) {
            error = a.error(result);
            clear();
            return false;
        }
        frame = a.av_frame_alloc();
        packet = a.av_packet_alloc();
        frame->format = codec->pix_fmt;
        frame->width = w;
        frame->height = h;
        if (a.av_frame_get_buffer(frame, 32) < 0) {
            error = "NV12 buffer allocation";
            clear();
            return false;
        }
        return true;
    }

    bool encode(const QImage& image, bool key, std::vector<QByteArray>* out, std::vector<bool>* keys) {
        auto& a = api();
        if (a.av_frame_make_writable(frame) < 0)
            return false;
        scaler = a.sws_getCachedContext(scaler, image.width(), image.height(), AV_PIX_FMT_BGRA, codec->width,
                                        codec->height, AV_PIX_FMT_NV12, SWS_FAST_BILINEAR, nullptr, nullptr,
                                        nullptr);
        if (!scaler) {
            error = "Video scaler";
            return false;
        }
        const uint8_t* planes[] = {image.constBits(), nullptr, nullptr, nullptr};
        int strides[] = {int(image.bytesPerLine()), 0, 0, 0};
        a.sws_scale(scaler, planes, strides, 0, image.height(), frame->data, frame->linesize);
        frame->pts = sequence++;
        frame->duration = 1;
        frame->pict_type = key ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
        auto drain = [&] {
            int result;
            while ((result = a.avcodec_receive_packet(codec, packet)) >= 0) {
                const bool k = (packet->flags & AV_PKT_FLAG_KEY) != 0;
                auto bytes = annexB(packet->data, packet->size);
                if (k)
                    bytes = annexB(codec->extradata, codec->extradata_size) + bytes;
                out->push_back(bytes);
                keys->push_back(k);
                a.av_packet_unref(packet);
            }
            return result;
        };
        int result = a.avcodec_send_frame(codec, frame);
        if (result == AVERROR(EAGAIN)) {
            drain();
            result = a.avcodec_send_frame(codec, frame);
        }
        if (result < 0) {
            error = a.error(result);
            return false;
        }
        result = drain();
        if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
            error = a.error(result);
            return false;
        }
        return true;
    }
};

} // namespace furina::video::detail
