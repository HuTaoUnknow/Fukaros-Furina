#include "screen_scale.h"
#include "peer.h"
#include "secure_store.h"
#include <QRandomGenerator>
#include <QDateTime>
#include <QBuffer>
#include <QImageReader>
#include <QNetworkProxy>
#include <QHostAddress>
#include "video_policy.h"

namespace {
void randomBytes(uint8_t* out, int n) {
    for (int i = 0; i < n;) {
        const quint32 value = QRandomGenerator::system()->generate();
        for (int shift = 0; shift < 32 && i < n; shift += 8)
            out[i++] = uint8_t(value >> shift);
    }
}

struct RecordKeys {
    uint8_t key[32]{};
    uint8_t salt[16]{};
    uint64_t counter = 0;
};

int takeRecord(QByteArray& cipher, RecordKeys& rec, QByteArray& plain) {
    if (cipher.size() < 4)
        return 0;
    const auto* data = reinterpret_cast<const uint8_t*>(cipher.constData());
    const auto len = fr_get_u32(data);
    if (len > FR_MAX_PAYLOAD + FR_HEADER_SIZE)
        return -1;
    if (cipher.size() < int(4 + 16 + len))
        return 0;
    plain.resize(int(len));
    size_t consumed = 0, out_n = 0;
    const int rc = fr_record_open(rec.key, rec.salt, &rec.counter, data, size_t(cipher.size()), &consumed,
                                  reinterpret_cast<uint8_t*>(plain.data()), size_t(plain.size()), &out_n);
    if (rc == 1) {
        cipher.remove(0, int(consumed));
        plain.resize(int(out_n));
    }
    return rc;
}
} // namespace

Peer::Peer(QObject* p) : QObject(p), m_cursor(this) {
    m_cursor.availability = [this](bool ready) { emit cursorConnection(ready); };
    m_cursor.packet = [this](const QByteArray& p) { emit cursor(p); };
    // Device transport is direct TCP over USB forwarding or the local network.
    m_server.setProxy(QNetworkProxy::NoProxy);
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        while (m_server.hasPendingConnections()) {
            auto* s = m_server.nextPendingConnection();
            if (m_socket) {
                if (m_ready)
                    m_cursor.accept(s);
                else {
                    s->abort();
                    s->deleteLater();
                }
                continue;
            }
            const auto now = QDateTime::currentMSecsSinceEpoch();
            if (m_offerUsed || (m_expiresAtMs && now > m_expiresAtMs) || !fr_attempt_begin(&m_gate, now)) {
                s->abort();
                s->deleteLater();
                emit status(m_offerUsed ? QString::fromUtf8(fr_security_error_text(FR_SEC_USED))
                            : m_expiresAtMs && now > m_expiresAtMs
                                ? QString::fromUtf8(fr_security_error_text(FR_SEC_EXPIRED))
                                : QString::fromUtf8(fr_security_error_text(FR_SEC_LIMIT)));
                continue;
            }
            attach(s, true);
        }
    });
    m_tick.setInterval(3000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (!m_socket)
            return;
        if ((!m_ready && m_session.elapsed() > 6000) || m_lastRx.elapsed() > 18000) {
            fail(!m_secure ? tr("Connection timeout. Update both devices to 0.4.21 if the peer is older.")
                           : tr("Connection timeout"));
            return;
        }
        if (m_ready)
            send(FR_PING);
    });
}

Peer::~Peer() {
    close();
}

void Peer::setOfferWindow(qint64 expiresAtMs, bool singleUse) {
    m_expiresAtMs = expiresAtMs;
    m_singleUse = singleUse;
}

void Peer::setExpectedPeer(const QByteArray& publicKey) {
    m_expectedPeer = publicKey.size() == 32 ? publicKey : QByteArray();
}

void Peer::useStoredIdentity() {
    m_storeIdentity = true;
    m_haveIdentity = false;
}

QByteArray Peer::localPublicKey() {
    ensureIdentity();
    return QByteArray(reinterpret_cast<const char*>(m_identity.public_key), 32);
}

QByteArray Peer::peerPublicKey() const {
    return m_sessionPeer;
}

void Peer::rememberOffer(qint64* expiry, bool* singleUse, QByteArray* expected) {
    *expiry = m_expiresAtMs;
    *singleUse = m_singleUse;
    *expected = m_expectedPeer;
}

void Peer::restoreOffer(qint64 expiry, bool singleUse, const QByteArray& expected) {
    m_expiresAtMs = expiry;
    m_singleUse = singleUse;
    m_expectedPeer = expected;
    m_offerUsed = false;
}

void Peer::ensureIdentity() {
    if (m_haveIdentity)
        return;
    QByteArray secret = m_storeIdentity ? SecureStore::load("x25519-static") : QByteArray();
    if (secret.size() != 32) {
        secret = QByteArray(32, Qt::Uninitialized);
        randomBytes(reinterpret_cast<uint8_t*>(secret.data()), 32);
        if (m_storeIdentity)
            SecureStore::save("x25519-static", secret);
    }
    fr_identity_from_secret(&m_identity, reinterpret_cast<const uint8_t*>(secret.constData()));
    secret.fill(0);
    m_haveIdentity = true;
}

bool Peer::takeCredential(const QString& token, bool listening) {
    uint8_t psk[32];
    fr_qr_info info{};
    int error = FR_SEC_FORMAT;
    const int kind = fr_credential_from_text(token.toUtf8().constData(), psk, &info, &error);
    if (!kind) {
        emit status(QString::fromUtf8(fr_security_error_text(error)));
        return false;
    }
    m_psk = QByteArray(reinterpret_cast<const char*>(psk), 32);
    memset(psk, 0, sizeof psk);
    if (kind == 2) {
        ensureIdentity();
        if (listening) {
            if (!fr_public_equal(info.public_key, m_identity.public_key)) {
                emit status(tr("QR code does not match this device"));
                return false;
            }
        } else {
            m_expectedPeer = QByteArray(reinterpret_cast<const char*>(info.public_key), 32);
            m_expiresAtMs = info.expiry_unix * 1000;
            m_singleUse = true;
        }
    }
    return true;
}

bool Peer::listen(quint16 port, const QString& token, const QString& bindAddress) {
    qint64 expiry = 0;
    bool singleUse = false;
    QByteArray expected;
    rememberOffer(&expiry, &singleUse, &expected);
    close();
    restoreOffer(expiry, singleUse, expected);
    if (!takeCredential(token, true))
        return false;
    QHostAddress address(QHostAddress::AnyIPv4);
    if (!bindAddress.trimmed().isEmpty() &&
        (!address.setAddress(bindAddress.trimmed()) || address.protocol() != QAbstractSocket::IPv4Protocol)) {
        emit status("Invalid listening IP address");
        return false;
    }
    m_server.setMaxPendingConnections(2);
    if (!m_server.listen(address, port)) {
        emit status(m_server.errorString());
        return false;
    }
    emit status(tr("Listening on port %1").arg(port));
    return true;
}

void Peer::connectTo(const QString& host, quint16 port, const QString& token, const QString& localAddress) {
    qint64 expiry = 0;
    bool singleUse = false;
    QByteArray expected;
    rememberOffer(&expiry, &singleUse, &expected);
    close();
    restoreOffer(expiry, singleUse, expected);
    if (!takeCredential(token, false)) {
        emit disconnected();
        return;
    }
    auto* s = new QTcpSocket(this);
    s->setProxy(QNetworkProxy::NoProxy);
    if (!localAddress.isEmpty()) {
        QHostAddress local;
        if (!local.setAddress(localAddress) || local.protocol() != QAbstractSocket::IPv4Protocol ||
            !s->bind(local, 0)) {
            emit status(
                "无法绑定 USB 网卡，连接已取消。\nCould not bind the USB adapter. Connection cancelled.");
            s->deleteLater();
            emit disconnected();
            return;
        }
    }
    attach(s, false);
    s->connectToHost(host, port);
}

void Peer::close(bool graceful) {
    m_cursor.close();
    m_server.close();
    m_tick.stop();
    const bool drain =
        graceful && m_ready && m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
    // TCP orders this ping after cancellation. Its pong acknowledges that the
    // remote endpoint processed the stop, rather than just accepting TCP bytes.
    if (drain)
        send(FR_PING);
    m_ready = false;
    m_screenScaleSupported = false;
    m_screenScaleToggleSupported = false;
    if (m_socket) {
        auto* s = m_socket;
        m_socket = nullptr;
        s->disconnect(this);
        if (drain && s->state() == QAbstractSocket::ConnectedState && m_pendingPings > 0) {
            RecordKeys recv, sendKeys;
            memcpy(recv.key, m_recvKey, 32);
            memcpy(recv.salt, m_recvSalt, 16);
            recv.counter = m_recvCounter;
            memcpy(sendKeys.key, m_sendKey, 32);
            memcpy(sendKeys.salt, m_sendSalt, 16);
            sendKeys.counter = m_sendCounter;
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            connect(s, &QTcpSocket::errorOccurred, s, [s](QAbstractSocket::SocketError) {
                s->abort();
                s->deleteLater();
            });
            connect(s, &QTcpSocket::readyRead, s,
                    [s, cipher = m_stream, pending = m_rx, recv, sendKeys, pongs = m_pendingPings]() mutable {
                        cipher += s->readAll();
                        if (cipher.size() > int(FR_MAX_PAYLOAD + FR_HEADER_SIZE + 64)) {
                            s->abort();
                            s->deleteLater();
                            return;
                        }
                        while (true) {
                            QByteArray frame;
                            const int rc = takeRecord(cipher, recv, frame);
                            if (rc == 0)
                                break;
                            if (rc < 0) {
                                s->abort();
                                s->deleteLater();
                                return;
                            }
                            pending += frame;
                            while (pending.size() >= FR_HEADER_SIZE) {
                                fr_header h{};
                                if (!fr_decode_header(reinterpret_cast<const uint8_t*>(pending.constData()), &h)) {
                                    s->abort();
                                    s->deleteLater();
                                    return;
                                }
                                if (pending.size() < FR_HEADER_SIZE + h.length)
                                    break;
                                pending.remove(0, FR_HEADER_SIZE + h.length);
                                if (h.type == FR_PING && h.length == 0) {
                                    uint8_t reply[FR_HEADER_SIZE];
                                    fr_encode_header(reply, FR_PONG, 0);
                                    QByteArray body(reinterpret_cast<char*>(reply), FR_HEADER_SIZE);
                                    QByteArray record(body.size() + 20, Qt::Uninitialized);
                                    size_t n = 0;
                                    if (fr_record_seal(sendKeys.key, sendKeys.salt, &sendKeys.counter,
                                                       reinterpret_cast<const uint8_t*>(body.constData()),
                                                       size_t(body.size()), reinterpret_cast<uint8_t*>(record.data()),
                                                       size_t(record.size()), &n) == 1) {
                                        s->write(record.constData(), qsizetype(n));
                                        s->flush();
                                    }
                                }
                                if (h.type == FR_PONG && h.length == 0 && --pongs == 0) {
                                    s->readAll();
                                    s->disconnectFromHost();
                                    if (s->state() == QAbstractSocket::UnconnectedState)
                                        s->deleteLater();
                                    return;
                                }
                            }
                        }
                    });
            QTimer::singleShot(5000, s, [s] {
                s->abort();
                s->deleteLater();
            });
            s->flush();
        } else {
            s->abort();
            s->deleteLater();
        }
        emit disconnected();
    }
    m_rx.clear();
    m_stream.clear();
    m_secure = false;
    m_phase = HsIdle;
    m_sendCounter = m_recvCounter = 0;
    m_sessionPeer.clear();
    fr_hs_wipe(&m_hs);
    m_backlog.invalidate();
    m_waitVideoKey = false;
    m_pendingPings = 0;
}

void Peer::attach(QTcpSocket* s, bool server) {
    s->setObjectName("furinaMain");
    m_socket = s;
    m_serverSide = server;
    m_ready = false;
    m_screenScaleSupported = false;
    m_screenScaleToggleSupported = false;
    m_pendingPings = 0;
    m_rx.clear();
    m_stream.clear();
    m_secure = false;
    m_phase = server ? HsWaitHello : HsIdle;
    m_sendCounter = m_recvCounter = 0;
    m_session.start();
    m_lastRx.start();
    s->setProxy(QNetworkProxy::NoProxy);
    s->setReadBufferSize(FR_MAX_PAYLOAD + FR_HEADER_SIZE);
    if (s->state() == QAbstractSocket::ConnectedState)
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(s, &QTcpSocket::connected, this, [this, s] {
        if (m_socket != s)
            return;
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        if (!m_serverSide)
            beginClient();
    });
    connect(s, &QTcpSocket::bytesWritten, this, [this] {
        if (m_socket && m_socket->bytesToWrite() == 0)
            m_backlog.invalidate();
    });
    connect(s, &QTcpSocket::readyRead, this, &Peer::read);
    connect(s, &QTcpSocket::errorOccurred, this, [this, s](QAbstractSocket::SocketError error) {
        if (m_socket != s)
            return;
        // FIN may arrive with unread final control messages in the receive buffer.
        if (error == QAbstractSocket::RemoteHostClosedError) {
            read();
            if (m_socket != s)
                return;
        }
        const auto reason = s->errorString();
        m_cursor.close();
        m_socket = nullptr;
        m_ready = false;
        m_screenScaleSupported = false;
        m_screenScaleToggleSupported = false;
        m_tick.stop();
        m_rx.clear();
        m_stream.clear();
        m_secure = false;
        m_phase = HsIdle;
        s->disconnect(this);
        s->abort();
        s->deleteLater();
        emit disconnected();
        emit status(reason);
    });
    connect(s, &QTcpSocket::disconnected, this, [this, s] {
        if (m_socket == s)
            read();
        if (m_socket == s) {
            m_cursor.close();
            m_socket = nullptr;
            m_ready = false;
            m_screenScaleSupported = false;
            m_screenScaleToggleSupported = false;
            m_tick.stop();
            m_rx.clear();
            m_stream.clear();
            m_secure = false;
            m_phase = HsIdle;
            emit disconnected();
            emit status(tr("Disconnected"));
        }
        s->deleteLater();
    });
    if (!server && s->state() == QAbstractSocket::ConnectedState)
        beginClient();
    m_tick.start();
}

void Peer::fail(const QString& reason) {
    if (!m_ready)
        fr_attempt_fail(&m_gate, QDateTime::currentMSecsSinceEpoch());
    emit status(reason);
    if (m_socket)
        m_socket->abort();
}

void Peer::read() {
    if (!m_socket)
        return;
    m_stream += m_socket->readAll();
    if (m_stream.size() > int(2 * (FR_MAX_PAYLOAD + FR_HEADER_SIZE + 32))) {
        fail(tr("Receive buffer exceeded"));
        return;
    }
    if (m_phase != HsOpen && !pumpHandshake())
        return;
    while (m_socket && m_phase == HsOpen) {
        QByteArray frame;
        RecordKeys recv;
        memcpy(recv.key, m_recvKey, 32);
        memcpy(recv.salt, m_recvSalt, 16);
        recv.counter = m_recvCounter;
        const int rc = takeRecord(m_stream, recv, frame);
        if (rc == 0)
            break;
        if (rc < 0) {
            fail(tr("Encrypted record rejected"));
            return;
        }
        m_recvCounter = recv.counter;
        m_rx += frame;
        while (m_rx.size() >= FR_HEADER_SIZE) {
            fr_header h{};
            if (!fr_decode_header(reinterpret_cast<const uint8_t*>(m_rx.constData()), &h)) {
                fail(tr("Invalid protocol header"));
                return;
            }
            if (m_rx.size() < FR_HEADER_SIZE + h.length)
                break;
            auto payload = m_rx.mid(FR_HEADER_SIZE, h.length);
            m_rx.remove(0, FR_HEADER_SIZE + h.length);
            m_lastRx.restart();
            handle(h.type, payload);
            if (!m_socket)
                return;
        }
    }
}

void Peer::handle(quint8 t, const QByteArray& p) {
    if (!m_ready) {
        if (!m_serverSide && m_secure && t == FR_READY && p.isEmpty()) {
            m_ready = true;
            startCursor();
            send(FR_STATUS, QByteArray("Furina capability: screen-scale-v2"));
            send(FR_STATUS, QByteArray("Furina capability: screen-scale-v1"));
            emit connected();
            emit status(tr("Paired"));
            return;
        }
        fail(tr("Authentication required"));
        return;
    }
    switch (t) {
    case FR_VIDEO: {
        fr_video_info info{};
        if (!fr_video_read(reinterpret_cast<const uint8_t*>(p.constData()), p.size(), &info)) {
            fail("Invalid video packet");
            return;
        }
        emit video(p);
        break;
    }
    case FR_CURSOR_PREFERENCE: {
        if (p.size() != 12) {
            fail("Invalid cursor preference");
            return;
        }
        auto d = reinterpret_cast<const uint8_t*>(p.constData());
        auto value = fr_get_u32(d);
        quint64 revision = (quint64(fr_get_u32(d + 4)) << 32) | fr_get_u32(d + 8);
        if (value > 1 || revision > quint64(INT64_MAX)) {
            fail("Invalid cursor preference");
            return;
        }
        emit cursorPreference(value != 0, revision);
        break;
    }
    case FR_SCREEN_SCALE: {
        if (p.size() != 12 && p.size() != 16) {
            fail("Invalid screen scale");
            return;
        }
        const auto* data = reinterpret_cast<const uint8_t*>(p.constData());
        const auto value = fr_get_u32(data);
        const quint64 revision = (quint64(fr_get_u32(data + 4)) << 32) | fr_get_u32(data + 8);
        const auto enabled = p.size() == 16 ? fr_get_u32(data + 12) : 1u;
        if (!fr_screen_scale_valid(value) || revision > quint64(INT64_MAX) || enabled > 1) {
            fail("Invalid screen scale");
            return;
        }
        emit screenScale(enabled ? int(value) : 100, revision);
        emit screenScalePreference(int(value), revision, enabled != 0);
        break;
    }
    case FR_POINTER_SPEED: {
        if (p.size() != 12) {
            fail("Invalid pointer speed");
            return;
        }
        const auto* data = reinterpret_cast<const uint8_t*>(p.constData());
        const auto percent = fr_get_u32(data);
        const quint64 revision = (quint64(fr_get_u32(data + 4)) << 32) | fr_get_u32(data + 8);
        if (percent < 25 || percent > 400 || revision > quint64(0x7fffffffffffffffULL)) {
            fail("Invalid pointer speed");
            return;
        }
        emit pointerSpeed(int(percent), revision);
        break;
    }
    case FR_PROFILE:
        if (p.size() != 4 ||
            !fr_resolution_valid(fr_get_u32(reinterpret_cast<const uint8_t*>(p.constData())))) {
            fail("Invalid video profile");
            return;
        }
        emit profile(int(fr_get_u32(reinterpret_cast<const uint8_t*>(p.constData()))));
        break;
    case FR_FEEDBACK: {
        if (p.size() != 16) {
            fail("Invalid video feedback");
            return;
        }
        const auto* data = reinterpret_cast<const uint8_t*>(p.constData());
        auto fps = fr_get_u32(data), work = fr_get_u32(data + 4), queue = fr_get_u32(data + 8),
             display = fr_get_u32(data + 12);
        if (fps > 240000 || work > 10000000 || queue > 10000 || display > 240000) {
            fail("Invalid video feedback");
            return;
        }
        emit feedback(fps / 1000.0, work / 1000.0, queue, display / 1000.0);
        break;
    }
    case FR_FRAME: {
        QBuffer b;
        b.setData(p);
        b.open(QIODevice::ReadOnly);
        QImageReader r(&b, "JPEG");
        const auto s = r.size();
        if (!s.isValid() || s.width() > 4096 || s.height() > 4096 ||
            qint64(s.width()) * s.height() > 16777216) {
            fail(tr("Invalid frame dimensions"));
            return;
        }
        auto im = r.read();
        if (im.isNull()) {
            fail(tr("Invalid JPEG"));
            return;
        }
        emit frame(im);
        break;
    }
    case FR_GESTURE: {
        fr_gesture g{};
        if (!fr_decode_gesture(reinterpret_cast<const uint8_t*>(p.constData()), p.size(), &g)) {
            fail(tr("Invalid gesture"));
            return;
        }
        emit gesture(g);
        break;
    }
    case FR_KEY:
        if (p.size() != 4) {
            fail(tr("Invalid key"));
            return;
        }
        emit key(fr_get_u32(reinterpret_cast<const uint8_t*>(p.constData())));
        break;
    case FR_STATUS:
        if (p.size() > 4096) {
            fail(tr("Invalid status"));
            return;
        }
        if (p == "Furina capability: screen-scale-v2") {
            if (!m_screenScaleToggleSupported) {
                m_screenScaleSupported = true;
                m_screenScaleToggleSupported = true;
                emit screenScaleAvailable();
            }
            break;
        }
        if (p == "Furina capability: screen-scale-v1") {
            if (!m_screenScaleSupported) {
                m_screenScaleSupported = true;
                emit screenScaleAvailable();
            }
            break;
        }
        emit status(QString::fromUtf8(p));
        break;
    case FR_APPROVAL: {
        if (p.size() != 12) {
            fail("Invalid approval request");
            return;
        }
        const auto* data = reinterpret_cast<const uint8_t*>(p.constData());
        const auto id = fr_get_u32(data), verb = fr_get_u32(data + 4), options = fr_get_u32(data + 8);
        if (!fr_valid_approval(id, verb, options)) {
            fail("Invalid approval request");
            return;
        }
        emit approval(id, verb, options);
        break;
    }
    case FR_SESSION: {
        if (p.size() != 4) {
            fail("Invalid session request");
            return;
        }
        const auto action = fr_get_u32(reinterpret_cast<const uint8_t*>(p.constData()));
        if (!fr_valid_session_action(action)) {
            fail("Invalid session request");
            return;
        }
        emit session(action);
        break;
    }
    case FR_PING:
        if (!p.isEmpty()) {
            fail(tr("Invalid ping"));
            return;
        }
        send(FR_PONG);
        break;
    case FR_PONG:
        if (!p.isEmpty())
            fail(tr("Invalid pong"));
        else if (m_pendingPings > 0)
            --m_pendingPings;
        break;
    default:
        fail(tr("Unexpected message"));
    }
}

void Peer::send(quint8 t, const QByteArray& p) {
    if (!m_socket || !m_secure || m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    uint8_t h[FR_HEADER_SIZE];
    if (!fr_encode_header(h, t, p.size()))
        return;
    if (m_socket->bytesToWrite() > 2 * 1024 * 1024) {
        if (t != FR_FRAME && t != FR_VIDEO)
            fail(tr("Peer too slow"));
        return;
    }
    QByteArray frame;
    frame.append(reinterpret_cast<const char*>(h), FR_HEADER_SIZE);
    frame.append(p);
    QByteArray record(frame.size() + 20, Qt::Uninitialized);
    size_t n = 0;
    if (fr_record_seal(m_sendKey, m_sendSalt, &m_sendCounter, reinterpret_cast<const uint8_t*>(frame.constData()),
                       size_t(frame.size()), reinterpret_cast<uint8_t*>(record.data()), size_t(record.size()),
                       &n) != 1) {
        fail(tr("Encrypted record rejected"));
        return;
    }
    if (t == FR_PING)
        ++m_pendingPings;
    m_socket->write(record.constData(), qsizetype(n));
}

bool Peer::canSendFrame() const {
    return m_ready && m_socket && m_socket->bytesToWrite() < 256 * 1024;
}

void Peer::sendFrame(const QImage& im) {
    if (!canSendFrame() || im.isNull())
        return;
    auto scaled = im;
    if (im.width() > 1280 || im.height() > 1280)
        scaled = im.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::FastTransformation);
    QByteArray jpeg;
    QBuffer b(&jpeg);
    b.open(QIODevice::WriteOnly);
    if (scaled.save(&b, "JPEG", 65))
        sendJpeg(jpeg);
}

void Peer::sendJpeg(const QByteArray& p) {
    if (canSendFrame())
        send(FR_FRAME, p);
}

void Peer::sendGesture(const fr_gesture& g) {
    uint8_t p[20];
    if (m_ready && fr_encode_gesture(p, &g))
        send(FR_GESTURE, QByteArray(reinterpret_cast<char*>(p), 20));
}

void Peer::sendKey(quint32 key) {
    uint8_t p[4];
    fr_put_u32(p, key);
    if (m_ready)
        send(FR_KEY, QByteArray(reinterpret_cast<char*>(p), 4));
}

void Peer::sendStatus(const QString& s) {
    if (m_ready)
        send(FR_STATUS, s.toUtf8().left(4096));
}

void Peer::sendSession(quint32 action) {
    uint8_t p[4];
    if (m_ready && fr_valid_session_action(action)) {
        fr_put_u32(p, action);
        send(FR_SESSION, QByteArray(reinterpret_cast<char*>(p), 4));
    }
}

void Peer::sendApproval(quint32 id, quint32 verb, quint32 options) {
    uint8_t data[12];
    if (m_ready && fr_valid_approval(id, verb, options)) {
        fr_put_u32(data, id);
        fr_put_u32(data + 4, verb);
        fr_put_u32(data + 8, options);
        send(FR_APPROVAL, QByteArray(reinterpret_cast<char*>(data), 12));
    }
}

unsigned Peer::videoQueueMs() const {
    return m_socket && m_socket->bytesToWrite() > 0 && m_backlog.isValid()
               ? unsigned(qMin<qint64>(10000, m_backlog.elapsed()))
               : 0;
}

bool Peer::sendVideo(const QByteArray& packet) {
    fr_video_info info{};
    if (!m_ready || !m_socket ||
        !fr_video_read(reinterpret_cast<const uint8_t*>(packet.constData()), packet.size(), &info))
        return false;
    if (m_socket->bytesToWrite() > 128 * 1024 || videoQueueMs() > 33) {
        m_waitVideoKey = true;
        return false;
    }
    if (m_waitVideoKey && !(info.flags & FR_VIDEO_KEY))
        return false;
    m_waitVideoKey = false;
    if (!m_backlog.isValid())
        m_backlog.start();
    send(FR_VIDEO, packet);
    return true;
}

void Peer::sendProfile(int edge) {
    uint8_t p[4];
    if (m_ready && fr_resolution_valid(edge)) {
        fr_put_u32(p, edge);
        send(FR_PROFILE, QByteArray(reinterpret_cast<char*>(p), 4));
    }
}

void Peer::sendFeedback(double fps, double workMs, unsigned queueMs, double displayedFps) {
    if (!m_ready)
        return;
    uint8_t p[16];
    fr_put_u32(p, uint32_t(qBound(0.0, fps, 240.0) * 1000));
    fr_put_u32(p + 4, uint32_t(qBound(0.0, workMs, 10000.0) * 1000));
    fr_put_u32(p + 8, qMin(10000u, queueMs));
    fr_put_u32(p + 12, uint32_t(qBound(0.0, displayedFps, 240.0) * 1000));
    send(FR_FEEDBACK, QByteArray(reinterpret_cast<char*>(p), 16));
}

void Peer::sendPointerSpeed(int percent, quint64 revision) {
    if (!m_ready || percent < 25 || percent > 400 || revision > quint64(0x7fffffffffffffffULL))
        return;
    uint8_t data[12];
    fr_put_u32(data, uint32_t(percent));
    fr_put_u32(data + 4, uint32_t(revision >> 32));
    fr_put_u32(data + 8, uint32_t(revision));
    send(FR_POINTER_SPEED, QByteArray(reinterpret_cast<char*>(data), 12));
}

void Peer::startCursor() {
    m_cursor.begin(QByteArray(reinterpret_cast<const char*>(m_cursorKey), 32), m_serverSide,
                   m_socket->peerAddress(), m_socket->peerPort(), m_socket->localAddress());
}

void Peer::beginClient() {
    if (!m_socket || m_serverSide || m_phase != HsIdle)
        return;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (m_expiresAtMs && now > m_expiresAtMs) {
        fail(QString::fromUtf8(fr_security_error_text(FR_SEC_EXPIRED)));
        return;
    }
    ensureIdentity();
    uint8_t eph[32], hello[FR_HS_CLIENT_HELLO];
    randomBytes(eph, 32);
    const uint8_t* pin = m_expectedPeer.size() == 32 ? reinterpret_cast<const uint8_t*>(m_expectedPeer.constData())
                                                     : nullptr;
    if (m_psk.size() != 32 ||
        !fr_hs_client_start(&m_hs, &m_identity, eph, reinterpret_cast<const uint8_t*>(m_psk.constData()), 1, pin,
                            hello)) {
        fail(QString::fromUtf8(fr_security_error_text(m_hs.error ? m_hs.error : FR_SEC_AUTH)));
        return;
    }
    m_socket->write(reinterpret_cast<const char*>(hello), sizeof hello);
    m_phase = HsWaitServer;
}

bool Peer::pumpHandshake() {
    if (m_stream.size() >= 4 && m_stream.left(4) == "FUR1") {
        fail(QString::fromUtf8(fr_security_error_text(FR_SEC_LEGACY)));
        return false;
    }
    if (m_phase == HsWaitHello) {
        if (m_stream.size() < FR_HS_CLIENT_HELLO)
            return true;
        ensureIdentity();
        uint8_t eph[32], reply[FR_HS_SERVER_HELLO];
        randomBytes(eph, 32);
        const auto pinBytes = !m_sessionPeer.isEmpty() ? m_sessionPeer : m_expectedPeer;
        const uint8_t* pin =
            pinBytes.size() == 32 ? reinterpret_cast<const uint8_t*>(pinBytes.constData()) : nullptr;
        if (m_psk.size() != 32 ||
            !fr_hs_server_start(&m_hs, &m_identity, eph, reinterpret_cast<const uint8_t*>(m_psk.constData()), pin,
                                reinterpret_cast<const uint8_t*>(m_stream.constData()), reply)) {
            fail(QString::fromUtf8(fr_security_error_text(m_hs.error ? m_hs.error : FR_SEC_AUTH)));
            return false;
        }
        m_socket->write(reinterpret_cast<const char*>(reply), sizeof reply);
        m_stream.remove(0, FR_HS_CLIENT_HELLO);
        m_phase = HsWaitConfirm;
    }
    if (m_phase == HsWaitServer) {
        if (m_stream.size() < FR_HS_SERVER_HELLO)
            return true;
        uint8_t confirm[FR_HS_CLIENT_CONFIRM];
        if (!fr_hs_client_finish(&m_hs, reinterpret_cast<const uint8_t*>(m_stream.constData()), confirm)) {
            fail(QString::fromUtf8(fr_security_error_text(m_hs.error ? m_hs.error : FR_SEC_AUTH)));
            return false;
        }
        m_socket->write(reinterpret_cast<const char*>(confirm), sizeof confirm);
        m_stream.remove(0, FR_HS_SERVER_HELLO);
        acceptSecure(false);
    }
    if (m_phase == HsWaitConfirm) {
        if (m_stream.size() < FR_HS_CLIENT_CONFIRM)
            return true;
        if (!fr_hs_server_finish(&m_hs, reinterpret_cast<const uint8_t*>(m_stream.constData()))) {
            fail(QString::fromUtf8(fr_security_error_text(m_hs.error ? m_hs.error : FR_SEC_AUTH)));
            return false;
        }
        m_stream.remove(0, FR_HS_CLIENT_CONFIRM);
        acceptSecure(true);
    }
    return true;
}

void Peer::acceptSecure(bool serverReady) {
    memcpy(m_sendKey, m_hs.send_key, 32);
    memcpy(m_recvKey, m_hs.recv_key, 32);
    memcpy(m_cursorKey, m_hs.cursor_key, 32);
    memcpy(m_sendSalt, m_hs.send_salt, 16);
    memcpy(m_recvSalt, m_hs.recv_salt, 16);
    m_sendCounter = m_recvCounter = 0;
    m_secure = true;
    m_phase = HsOpen;
    m_sessionPeer = QByteArray(reinterpret_cast<const char*>(m_hs.peer_static), 32);
    if (m_singleUse)
        m_offerUsed = true;
    fr_attempt_success(&m_gate);
    if (!serverReady)
        return;
    m_ready = true;
    startCursor();
    send(FR_READY);
    send(FR_STATUS, QByteArray("Furina capability: screen-scale-v2"));
    send(FR_STATUS, QByteArray("Furina capability: screen-scale-v1"));
    emit connected();
    emit status(tr("Paired"));
}

void Peer::sendCursorPreference(bool enabled, quint64 revision) {
    if (!m_ready || revision > quint64(INT64_MAX))
        return;
    uint8_t p[12];
    fr_put_u32(p, enabled ? 1 : 0);
    fr_put_u32(p + 4, revision >> 32);
    fr_put_u32(p + 8, uint32_t(revision));
    send(FR_CURSOR_PREFERENCE, QByteArray(reinterpret_cast<char*>(p), 12));
}

void Peer::sendScreenScale(int percent, quint64 revision, bool enabled) {
    if (!m_ready || !m_screenScaleSupported || !fr_screen_scale_valid(percent) ||
        revision > quint64(INT64_MAX))
        return;
    // v1 peers understand the effective scale only; never send them a larger packet.
    uint8_t p[16];
    fr_put_u32(p, m_screenScaleToggleSupported || enabled ? percent : 100);
    fr_put_u32(p + 4, revision >> 32);
    fr_put_u32(p + 8, uint32_t(revision));
    fr_put_u32(p + 12, enabled ? 1 : 0);
    send(FR_SCREEN_SCALE, QByteArray(reinterpret_cast<char*>(p), m_screenScaleToggleSupported ? 16 : 12));
}
