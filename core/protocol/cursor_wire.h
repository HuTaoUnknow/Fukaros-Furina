#pragma once
#include <QByteArray>
#include "furina_protocol.h"

// Dedicated cursor connection: bounded raw shape data, never video frames.
inline bool fr_cursor_packet_valid(const QByteArray& p) {
    if (p.size() < 4)
        return false;
    auto d = reinterpret_cast<const uint8_t*>(p.constData());
    auto type = fr_get_u32(d);
    if (type == 1) {
        if (p.size() != 24)
            return false;
        auto w = fr_get_u32(d + 12), h = fr_get_u32(d + 16);
        auto x = qint32(fr_get_u32(d + 4)), y = qint32(fr_get_u32(d + 8));
        return w > 0 && h > 0 && w <= 32768 && h <= 32768 && x >= -512 && y >= -512 && x < int(w) &&
               y < int(h) && fr_get_u32(d + 20) <= 1;
    }
    if (type == 2) {
        if (p.size() < 16)
            return false;
        auto kind = fr_get_u32(d + 4), w = fr_get_u32(d + 8), h = fr_get_u32(d + 12);
        return (kind == 1 || kind == 2 || kind == 4) && w && h && w <= 512 && h <= 512 &&
               p.size() == 16 + int(w * h * 4);
    }
    return false;
}

inline QByteArray fr_cursor_position(int x, int y, int w, int h, bool visible) {
    QByteArray p(24, Qt::Uninitialized);
    auto d = reinterpret_cast<uint8_t*>(p.data());
    fr_put_u32(d, 1);
    fr_put_u32(d + 4, quint32(x));
    fr_put_u32(d + 8, quint32(y));
    fr_put_u32(d + 12, w);
    fr_put_u32(d + 16, h);
    fr_put_u32(d + 20, visible ? 1 : 0);
    return p;
}
