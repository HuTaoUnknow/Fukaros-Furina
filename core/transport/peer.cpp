#include "peer.h"
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QBuffer>
#include <QImageReader>
#include <QNetworkProxy>
#include "video_policy.h"

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
            attach(s, true);
        }
    });
    m_tick.setInterval(3000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (!m_socket)
            return;
        if ((!m_ready && m_session.elapsed() > 6000) || m_lastRx.elapsed() > 18000) {
            fail(tr("Connection timeout"));
            return;
        }
        if (m_ready)
            send(FR_PING);
    });
}

Peer::~Peer() {
    close();
}

bool Peer::listen(quint16 port, const QString& token, const QString& bindAddress) {
    close();
    if (token.size() < 6) {
        emit status(tr("Pairing code requires at least 6 characters"));
        return false;
    }
    m_token = token;
    QHostAddress address(QHostAddress::AnyIPv4);
    if (!bindAddress.trimmed().isEmpty() &&
        (!address.setAddress(bindAddress.trimmed()) || address.protocol() != QAbstractSocket::IPv4Protocol)) {
        emit status("Invalid listening IP address");
        return false;
    }
    if (!m_server.listen(address, port)) {
        emit status(m_server.errorString());
        return false;
    }
    emit status(tr("Listening on port %1").arg(port));
    return true;
}

void Peer::connectTo(const QString& host, quint16 port, const QString& token, const QString& localAddress) {
    close();
    if (token.size() < 6) {
        emit status(tr("Enter the tablet pairing code (at least 6 characters)"));
        return;
    }
    m_token = token;
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
    if (m_socket) {
        auto* s = m_socket;
        m_socket = nullptr;
        s->disconnect(this);
        if (drain && s->state() == QAbstractSocket::ConnectedState && m_pendingPings > 0) {
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            connect(s, &QTcpSocket::errorOccurred, s, [s](QAbstractSocket::SocketError) {
                s->abort();
                s->deleteLater();
            });
            connect(s, &QTcpSocket::readyRead, s, [s, pending = m_rx, pongs = m_pendingPings]() mutable {
                while (s->bytesAvailable()) {
                    const auto budget = qint64(FR_MAX_PAYLOAD + FR_HEADER_SIZE) - pending.size();
                    if (budget <= 0) {
                        s->abort();
                        s->deleteLater();
                        return;
                    }
                    pending += s->read(qMin(budget, s->bytesAvailable()));
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
                            s->write(reinterpret_cast<const char*>(reply), sizeof reply);
                            s->flush();
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
    m_challenge.clear();
    m_backlog.invalidate();
    m_waitVideoKey = false;
    m_pendingPings = 0;
}

void Peer::attach(QTcpSocket* s, bool server) {
    s->setObjectName("furinaMain");
    m_socket = s;
    m_serverSide = server;
    m_ready = false;
    m_pendingPings = 0;
    m_rx.clear();
    m_session.start();
    m_lastRx.start();
    s->setProxy(QNetworkProxy::NoProxy);
    s->setReadBufferSize(FR_MAX_PAYLOAD + FR_HEADER_SIZE);
    if (s->state() == QAbstractSocket::ConnectedState)
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(s, &QTcpSocket::connected, s, [s] { s->setSocketOption(QAbstractSocket::LowDelayOption, 1); });
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
        m_tick.stop();
        m_rx.clear();
        m_challenge.clear();
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
            m_tick.stop();
            m_rx.clear();
            emit disconnected();
            emit status(tr("Disconnected"));
        }
        s->deleteLater();
    });
    if (server) {
        m_challenge.resize(32);
        for (int i = 0; i < 32; i += 4) {
            quint32 r = QRandomGenerator::system()->generate();
            fr_put_u32(reinterpret_cast<uint8_t*>(m_challenge.data() + i), r);
        }
        send(FR_CHALLENGE, m_challenge);
    }
    m_tick.start();
}

void Peer::fail(const QString& reason) {
    emit status(reason);
    if (m_socket)
        m_socket->abort();
}

void Peer::read() {
    while (m_socket && m_socket->bytesAvailable()) {
        const auto budget = qint64(FR_MAX_PAYLOAD + FR_HEADER_SIZE) - m_rx.size();
        if (budget <= 0) {
            fail(tr("Receive buffer exceeded"));
            return;
        }
        m_rx += m_socket->read(qMin(budget, m_socket->bytesAvailable()));
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
        if (!m_serverSide && t == FR_CHALLENGE && p.size() == 32 && m_challenge.isEmpty()) {
            m_challenge = p;
            send(FR_AUTH, QMessageAuthenticationCode::hash(p, m_token.toUtf8(), QCryptographicHash::Sha256));
            return;
        }
        if (m_serverSide && t == FR_AUTH && p.size() == 32) {
            const auto expected =
                QMessageAuthenticationCode::hash(m_challenge, m_token.toUtf8(), QCryptographicHash::Sha256);
            unsigned diff = 0;
            for (int i = 0; i < 32; ++i)
                diff |= unsigned(uint8_t(p[i]) ^ uint8_t(expected[i]));
            if (diff) {
                fail(tr("Pairing code rejected"));
                return;
            }
            m_ready = true;
            startCursor();
            send(FR_READY);
            emit connected();
            emit status(tr("Paired"));
            return;
        }
        if (!m_serverSide && t == FR_READY && p.isEmpty() && m_challenge.size() == 32) {
            m_ready = true;
            startCursor();
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
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    uint8_t h[FR_HEADER_SIZE];
    if (!fr_encode_header(h, t, p.size()))
        return;
    if (m_socket->bytesToWrite() > 2 * 1024 * 1024) {
        if (t != FR_FRAME && t != FR_VIDEO)
            fail(tr("Peer too slow"));
        return;
    }
    if (t == FR_PING)
        ++m_pendingPings;
    m_socket->write(reinterpret_cast<const char*>(h), sizeof h);
    m_socket->write(p);
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
    const auto key = QMessageAuthenticationCode::hash(QByteArray("FURINA-CURSOR-1") + m_challenge,
                                                      m_token.toUtf8(), QCryptographicHash::Sha256);
    m_cursor.begin(key, m_serverSide, m_socket->peerAddress(), m_socket->peerPort(),
                   m_socket->localAddress());
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
