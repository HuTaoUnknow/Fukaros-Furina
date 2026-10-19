#pragma once
#include "furina_protocol.h"
#include <QCoreApplication>
#include <QLibrary>
#include <QImage>
#include <QTransform>
#include <QByteArray>
#include <vector>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mmsystem.h>
#include "desktop_cursor.h"
#include "desktop_transform.h"
#include "desktop_wgc.h"
#include "nvcodec/nvEncodeAPI.h"
extern "C" {
#include "ffmpeg/libavcodec/avcodec.h"
#include "ffmpeg/libavutil/opt.h"
#include "ffmpeg/libavutil/hwcontext.h"
#include "ffmpeg/libswscale/swscale.h"
}

namespace furina::video::detail {
// Keep the function lists and their expansions aligned; clang-format cannot parse these declarations.
// clang-format off
#define CODEC_FUNCTIONS(X) \
    X(avcodec_version) \
    X(avcodec_find_encoder_by_name) \
    X(avcodec_find_decoder_by_name) \
    X(avcodec_alloc_context3) \
    X(avcodec_open2) \
    X(avcodec_free_context) \
    X(avcodec_send_frame) \
    X(avcodec_receive_packet) \
    X(avcodec_send_packet) \
    X(avcodec_receive_frame) \
    X(avcodec_flush_buffers) \
    X(av_packet_alloc) \
    X(av_packet_free) \
    X(av_packet_unref) \
    X(av_new_packet)
#define UTIL_FUNCTIONS(X) \
    X(av_frame_alloc) \
    X(av_frame_free) \
    X(av_frame_unref) \
    X(av_frame_get_buffer) \
    X(av_frame_make_writable) \
    X(av_opt_set) \
    X(av_opt_set_int) \
    X(av_strerror) \
    X(av_buffer_unref) \
    X(av_buffer_ref) \
    X(av_hwdevice_ctx_create) \
    X(av_hwframe_transfer_data)
#define SCALE_FUNCTIONS(X) \
    X(sws_getCachedContext) \
    X(sws_scale) \
    X(sws_freeContext) \
    X(sws_getCoefficients) \
    X(sws_setColorspaceDetails)
struct Api {
    QLibrary codec, util, scale;
    QString failure;
#define DECLARE(name) decltype(&::name) name = nullptr;

    CODEC_FUNCTIONS(DECLARE)

    UTIL_FUNCTIONS(DECLARE)
    SCALE_FUNCTIONS(DECLARE)
#undef DECLARE
    Api() {
        auto load = [](QLibrary& lib, const QString& name) {
            lib.setFileName(QCoreApplication::applicationDirPath() + "/" + name);
            if (lib.load())
                return true;
            lib.setFileName(name);
            return lib.load();
        };
        if (!load(util, "avutil-59") || !load(codec, "avcodec-61") || !load(scale, "swscale-8")) {
            failure = "FFmpeg 7.1 DLL missing: " + codec.errorString();
            return;
        }
#define LOAD_CODEC(name)                                                                                     \
    name = reinterpret_cast<decltype(name)>(codec.resolve(#name));                                           \
    if (!name) {                                                                                             \
        failure = #name;                                                                                     \
        return;                                                                                              \
    }
#define LOAD_UTIL(name)                                                                                      \
    name = reinterpret_cast<decltype(name)>(util.resolve(#name));                                            \
    if (!name) {                                                                                             \
        failure = #name;                                                                                     \
        return;                                                                                              \
    }
#define LOAD_SCALE(name)                                                                                     \
    name = reinterpret_cast<decltype(name)>(scale.resolve(#name));                                           \
    if (!name) {                                                                                             \
        failure = #name;                                                                                     \
        return;                                                                                              \
    }
        CODEC_FUNCTIONS(LOAD_CODEC)
        UTIL_FUNCTIONS(LOAD_UTIL)
        SCALE_FUNCTIONS(LOAD_SCALE)
#undef LOAD_CODEC
#undef LOAD_UTIL
#undef LOAD_SCALE
        if ((avcodec_version() >> 16) != 61)
            failure = "Incompatible avcodec ABI; requires major 61";
    }

    QString error(int code) {
        char buffer[256]{};
        av_strerror(code, buffer, sizeof buffer);
        return QString::fromUtf8(buffer);
    }
};

// clang-format on

inline Api& api() {
    static Api value;
    return value;
}

template <class T> struct Com {
    T* p = nullptr;

    ~Com() {
        clear();
    }

    void clear() {
        if (p)
            p->Release();
        p = nullptr;
    }

    T** put() {
        clear();
        return &p;
    }

    T* operator->() const {
        return p;
    }

    operator bool() const {
        return p;
    }
};

} // namespace furina::video::detail
