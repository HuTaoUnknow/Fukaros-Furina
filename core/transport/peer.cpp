#include "screen_scale.h"
#include "peer.h"
#include "furina_channel.h"
#include "handshake_limiter.h"
#include "monocypher.h"
#include "video_policy.h"
#include <QBuffer>
#include <QDateTime>
#include <QImageReader>
#include <QMutexLocker>
#include <QNetworkProxy>
#include <QSet>
#include <algorithm>
#include <cstring>

namespace {
QString lockoutStatus() {
    return QString("握手失败次数过多，已暂时锁定。\nToo many failed handshakes. Temporarily locked out.");
}

QString oneTimeStatus() {
    return QString("一次性配对密钥已使用，已拒绝。\nOne-time pairing secret was already used. Connection rejected.");
}

bool oneTimeToken(const QByteArray& token) {
    if (token.size() != 26 || !token.startsWith("ot1."))
        return false;
    for (int i = 4; i < token.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(token.at(i));
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok)
            return false;
    }
    return true;
}

QByteArray tokenFingerprint(const QByteArray& token) {
    QByteArray out(32, 0);
    crypto_blake2b(reinterpret_cast<uint8_t*>(out.data()), 32, reinterpret_cast<const uint8_t*>(token.constData()),
                   size_t(token.size()));
    return out;
}

QMutex g_usedLock;
QSet<QByteArray> g_usedTokens;

bool oneTimeUsed(const QByteArray& token) {
    QMutexLocker lock(&g_usedLock);
    return g_usedTokens.contains(tokenFingerprint(token));
}

void consumeOneTime(const QByteArray& token) {
    QMutexLocker lock(&g_usedLock);
    g_usedTokens.insert(tokenFingerprint(token));
}

struct WireItem {
    bool video = false;
    int left = 0;
};
}

class PeerIo : public QObject {
  public:
    explicit PeerIo(Peer* owner)
        : QObject(), peer(owner), server(new QTcpServer(this)), tick(new QTimer(this)), cursor(this) {
        server->setProxy(QNetworkProxy::NoProxy);
        tick->setInterval(3000);
        cursor.availability = [this](bool ready) {
            peer->m_cursorReady.store(ready);
            emit peer->cursorConnection(ready);
        };
        cursor.packet = [this](const QByteArray& data) { emit peer->cursor(data); };
        cursor.accounting = [this](int delta) { peer->m_openSockets.fetch_add(delta); };
        connect(server, &QTcpServer::newConnection, this, [this] { acceptPending(); });
        connect(tick, &QTimer::timeout, this, [this] { onTick(); });
    }

    Peer* peer;
    QTcpServer* server;
    QTimer* tick;
    CursorTransport cursor;
    FurinaChannel channel;
    HandshakeLimiter limiter;
    QTcpSocket* socket = nullptr;
    QString token;
    QByteArray tokenBytes;
    QElapsedTimer sessionClock, lastRx;
    QList<WireItem> wire;
    bool serverSide = false;
    bool appReady = false;
    bool failed = false;
    bool localClose = false;
    bool downEmitted = false;
    bool failureNoted = false;
    bool draining = false;
    bool usingLongTerm = false;
    bool usingOneTime = false;
    int pendingPings = 0;
    bool screenScaleSupported = false;
    bool screenScaleToggleSupported = false;

    void rememberBind() {
        QMutexLocker lock(&peer->m_infoLock);
        peer->m_bound = server->isListening() ? server->serverAddress() : QHostAddress();
        peer->m_port = server->isListening() ? server->serverPort() : 0;
    }

    QByteArray currentLongTerm() const {
        QMutexLocker lock(&peer->m_infoLock);
        return peer->m_longTermKey;
    }

    bool listen(quint16 port, const QString& pairing, const QString& bindAddress, const QByteArray& longTerm) {
        token = pairing;
        tokenBytes = pairing.toUtf8();
        usingOneTime = oneTimeToken(tokenBytes) && longTerm.size() != 32;
        QHostAddress address;
        const auto requested = bindAddress.trimmed();
        if (requested.isEmpty())
            address = QHostAddress(QHostAddress::AnyIPv4);
        else if (!address.setAddress(requested) || address.protocol() != QAbstractSocket::IPv4Protocol) {
            emit peer->status("Invalid listening IP address");
            rememberBind();
            return false;
        }
        if (!server->listen(address, port)) {
            emit peer->status(server->errorString());
            rememberBind();
            return false;
        }
        const bool wantAny = requested.isEmpty() || address == QHostAddress(QHostAddress::AnyIPv4) ||
                             address == QHostAddress(QHostAddress::Any);
        const auto actual = server->serverAddress();
        if (!wantAny && actual != address) {
            server->close();
            emit peer->status("监听地址未按指定绑定，已取消。\nListening address was not honored. Bind aborted.");
            rememberBind();
            return false;
        }
        rememberBind();
        emit peer->status(Peer::tr("Listening on port %1").arg(server->serverPort()));
        return true;
    }

    void connectToHost(const QString& host, quint16 port, const QString& pairing, const QString& localAddress,
                       const QByteArray& longTerm) {
        token = pairing;
        tokenBytes = pairing.toUtf8();
        usingOneTime = oneTimeToken(tokenBytes) && longTerm.size() != 32;
        auto* next = new QTcpSocket(this);
        next->setProxy(QNetworkProxy::NoProxy);
        if (!localAddress.isEmpty()) {
            QHostAddress local;
            if (!local.setAddress(localAddress) || local.protocol() != QAbstractSocket::IPv4Protocol ||
                !next->bind(local, 0)) {
                emit peer->status(
                    "无法绑定 USB 网卡，连接已取消。\nCould not bind the USB adapter. Connection cancelled.");
                next->deleteLater();
                emitDown();
                return;
            }
        }
        attach(next, false);
        next->connectToHost(host, port);
    }

    void close(bool graceful) {
        localClose = true;
        tick->stop();
        server->close();
        rememberBind();
        const bool live = socket && socket->state() == QAbstractSocket::ConnectedState && channel.transportReady();
        peer->m_ready.store(false);
        appReady = false;
        if (graceful && live) {
            draining = true;
            cursor.close();
            sendPlain(FR_PING, {}, false);
            emitDown();
            QTimer::singleShot(5000, this, [this] {
                if (draining)
                    dropSocket();
            });
            return;
        }
        const bool had = socket != nullptr;
        dropSocket();
        if (had)
            emitDown();
    }

    void acceptPending() {
        while (server->hasPendingConnections()) {
            auto* next = server->nextPendingConnection();
            const auto address = next->peerAddress().toString();
            if (!limiter.allow(address)) {
                emit peer->status(lockoutStatus());
                next->abort();
                next->deleteLater();
                continue;
            }
            if (socket) {
                if (appReady)
                    cursor.accept(next);
                else {
                    next->abort();
                    next->deleteLater();
                }
                continue;
            }
            attach(next, true);
        }
    }

    void attach(QTcpSocket* next, bool serverRole) {
        next->setObjectName("furinaMain");
        socket = next;
        peer->m_mainSocket.store(next);
        peer->m_openSockets.fetch_add(1);
        connect(next, &QObject::destroyed, this, [this] { peer->m_openSockets.fetch_sub(1); });
        serverSide = serverRole;
        appReady = false;
        failed = false;
        localClose = false;
        downEmitted = false;
        failureNoted = false;
        draining = false;
        pendingPings = 0;
        screenScaleSupported = false;
        screenScaleToggleSupported = false;
        wire.clear();
        peer->m_ready.store(false);
        peer->m_needVideoKey.store(false);
        peer->m_bytesToWrite.store(0);
        peer->m_backlogStartMs.store(0);
        sessionClock.start();
        lastRx.start();
        const auto longTerm = currentLongTerm();
        usingLongTerm = longTerm.size() == 32;
        usingOneTime = oneTimeToken(tokenBytes) && !usingLongTerm;
        channel.reset(serverRole ? FurinaChannel::Role::Server : FurinaChannel::Role::Client,
                      usingLongTerm ? longTerm : tokenBytes, usingLongTerm ? longTerm : QByteArray());
        next->setProxy(QNetworkProxy::NoProxy);
        next->setReadBufferSize(FR_MAX_PAYLOAD + FR_HEADER_SIZE + 64);
        connect(next, &QTcpSocket::connected, this, [this, next] {
            if (socket != next)
                return;
            next->setSocketOption(QAbstractSocket::LowDelayOption, 1);
            sendHello();
        });
        connect(next, &QTcpSocket::bytesWritten, this, [this](qint64 count) { onWritten(count); });
        connect(next, &QTcpSocket::readyRead, this, [this] { onRead(); });
        connect(next, &QTcpSocket::errorOccurred, this, [this, next](QAbstractSocket::SocketError error) {
            if (socket != next || failed)
                return;
            if (error == QAbstractSocket::RemoteHostClosedError) {
                onRead();
                if (socket != next)
                    return;
            }
            const auto reason = next->errorString();
            if (!localClose)
                noteFailure();
            dropSocket();
            if (!localClose)
                emit peer->status(reason);
            emitDown();
        });
        connect(next, &QTcpSocket::disconnected, this, [this, next] {
            if (socket == next)
                onRead();
            if (socket != next) {
                next->deleteLater();
                return;
            }
            if (!localClose)
                noteFailure();
            dropSocket();
            if (!localClose && !failed)
                emit peer->status(Peer::tr("Disconnected"));
            emitDown();
        });
        if (next->state() == QAbstractSocket::ConnectedState) {
            next->setSocketOption(QAbstractSocket::LowDelayOption, 1);
            sendHello();
        }
        tick->start();
    }

    void sendHello() {
        if (!socket || channel.transportReady())
            return;
        const auto hello = channel.start();
        if (hello.isEmpty()) {
            fail(channel.error().isEmpty() ? QString("握手无法开始。\nHandshake could not start.") : channel.error());
            return;
        }
        rawWrite(hello, false);
    }

    void onTick() {
        if (!socket)
            return;
        if ((!appReady && sessionClock.elapsed() > 6000) || lastRx.elapsed() > 18000) {
            fail(Peer::tr("Connection timeout"));
            return;
        }
        if (appReady)
            sendPlain(FR_PING, {}, false);
    }

    void onRead() {
        if (!socket)
            return;
        QByteArray egress;
        if (!channel.push(socket->readAll(), &egress)) {
            fail(channel.error().isEmpty() ? QString("握手失败，已拒绝。\nHandshake failed. Connection rejected.")
                                           : channel.error());
            return;
        }
        if (!egress.isEmpty())
            rawWrite(egress, false);
        quint8 type = 0;
        QByteArray payload;
        while (channel.pop(&type, &payload)) {
            lastRx.restart();
            if (draining) {
                if (type == FR_PING && payload.isEmpty())
                    sendPlain(FR_PONG, {}, false);
                else if (type == FR_PONG && payload.isEmpty() && pendingPings > 0 && --pendingPings == 0)
                    socket->disconnectFromHost();
                continue;
            }
            if (!appReady) {
                if (!serverSide && type == FR_READY && payload.isEmpty()) {
                    becomeReady();
                    continue;
                }
                fail(Peer::tr("Authentication required"));
                return;
            }
            handle(type, payload);
            if (!socket)
                return;
        }
        if (serverSide && !appReady && channel.transportReady())
            becomeReady();
    }

    void becomeReady() {
        if (appReady || !socket)
            return;
        appReady = true;
        peer->m_ready.store(true);
        limiter.succeed(socket->peerAddress().toString());
        if (usingOneTime)
            consumeOneTime(tokenBytes);
        const auto key = channel.longTermKey();
        if (key.size() == 32) {
            {
                QMutexLocker lock(&peer->m_infoLock);
                peer->m_longTermKey = key;
                peer->m_established = key;
            }
            emit peer->longTermKeyEstablished(key);
        }
        if (serverSide)
            sendPlain(FR_READY, {}, false);
        sendPlain(FR_STATUS, QByteArray("Furina capability: screen-scale-v2"), false);
        sendPlain(FR_STATUS, QByteArray("Furina capability: screen-scale-v1"), false);
        cursor.begin(channel.cursorSecret(), serverSide, socket->peerAddress(), socket->peerPort(),
                     socket->localAddress());
        emit peer->connected();
        emit peer->status(Peer::tr("Paired"));
    }

    void noteFailure() {
        if (failureNoted || appReady || !socket)
            return;
        failureNoted = true;
        limiter.fail(socket->peerAddress().toString());
    }

    void fail(const QString& reason) {
        if (failed)
            return;
        failed = true;
        noteFailure();
        emit peer->status(reason);
        dropSocket();
        emitDown();
    }

    void dropSocket() {
        draining = false;
        tick->stop();
        cursor.close();
        channel.wipe();
        appReady = false;
        peer->m_ready.store(false);
        peer->m_cursorReady.store(false);
        for (const auto& item : wire)
            if (item.video)
                peer->m_videoInFlight.fetch_sub(1);
        wire.clear();
        peer->m_bytesToWrite.store(0);
        peer->m_backlogStartMs.store(0);
        if (!socket)
            return;
        auto* old = socket;
        socket = nullptr;
        QTcpSocket* expected = old;
        peer->m_mainSocket.compare_exchange_strong(expected, nullptr);
        old->disconnect(this);
        peer->m_openSockets.fetch_sub(1);
        old->abort();
        old->deleteLater();
    }

    void emitDown() {
        if (downEmitted)
            return;
        downEmitted = true;
        emit peer->disconnected();
    }

    void rawWrite(const QByteArray& bytes, bool video) {
        if (!socket || bytes.isEmpty()) {
            if (video)
                peer->m_videoInFlight.fetch_sub(1);
            return;
        }
        socket->write(bytes);
        wire.append({video, int(bytes.size())});
        publishBacklog();
    }

    void onWritten(qint64 count) {
        auto left = count;
        while (left > 0 && !wire.isEmpty()) {
            auto& front = wire.first();
            if (front.left <= left) {
                left -= front.left;
                if (front.video)
                    peer->m_videoInFlight.fetch_sub(1);
                wire.removeFirst();
            } else {
                front.left -= int(left);
                left = 0;
            }
        }
        publishBacklog();
    }

    void publishBacklog() {
        const qint64 pending = socket ? socket->bytesToWrite() : 0;
        peer->m_bytesToWrite.store(pending);
        if (pending > 0) {
            qint64 zero = 0;
            peer->m_backlogStartMs.compare_exchange_strong(zero, QDateTime::currentMSecsSinceEpoch());
        } else
            peer->m_backlogStartMs.store(0);
    }

    void sendPlain(quint8 type, const QByteArray& payload, bool video) {
        if (!socket || socket->state() != QAbstractSocket::ConnectedState || !channel.transportReady()) {
            if (video)
                peer->m_videoInFlight.fetch_sub(1);
            return;
        }
        if (socket->bytesToWrite() > 2 * 1024 * 1024 && type != FR_FRAME && type != FR_VIDEO) {
            if (video)
                peer->m_videoInFlight.fetch_sub(1);
            fail(Peer::tr("Peer too slow"));
            return;
        }
        QByteArray wireBytes;
        if (!channel.seal(type, payload, &wireBytes)) {
            if (video)
                peer->m_videoInFlight.fetch_sub(1);
            fail("加密发送失败，已断开。\nEncrypted send failed. Connection closed.");
            return;
        }
        if (type == FR_PING)
            ++pendingPings;
        rawWrite(wireBytes, video);
    }

    void sendFrame(const QImage& image) {
        if (!appReady || image.isNull() || !socket || socket->bytesToWrite() >= 256 * 1024)
            return;
        auto scaled = image;
        if (image.width() > 1280 || image.height() > 1280)
            scaled = image.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::FastTransformation);
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        if (scaled.save(&buffer, "JPEG", 65))
            sendPlain(FR_FRAME, jpeg, false);
    }

    void handle(quint8 type, const QByteArray& payload) {
        switch (type) {
        case FR_VIDEO: {
            fr_video_info info{};
            if (!fr_video_read(reinterpret_cast<const uint8_t*>(payload.constData()), payload.size(), &info)) {
                fail("Invalid video packet");
                return;
            }
            emit peer->video(payload);
            break;
        }
        case FR_CURSOR_PREFERENCE: {
            if (payload.size() != 12) {
                fail("Invalid cursor preference");
                return;
            }
            auto data = reinterpret_cast<const uint8_t*>(payload.constData());
            auto value = fr_get_u32(data);
            quint64 revision = (quint64(fr_get_u32(data + 4)) << 32) | fr_get_u32(data + 8);
            if (value > 1 || revision > quint64(INT64_MAX)) {
                fail("Invalid cursor preference");
                return;
            }
            emit peer->cursorPreference(value != 0, revision);
            break;
        }
        case FR_SCREEN_SCALE: {
            if (payload.size() != 12 && payload.size() != 16) {
                fail("Invalid screen scale");
                return;
            }
            const auto* data = reinterpret_cast<const uint8_t*>(payload.constData());
            const auto value = fr_get_u32(data);
            const quint64 revision = (quint64(fr_get_u32(data + 4)) << 32) | fr_get_u32(data + 8);
            const auto enabled = payload.size() == 16 ? fr_get_u32(data + 12) : 1u;
            if (!fr_screen_scale_valid(value) || revision > quint64(INT64_MAX) || enabled > 1) {
                fail("Invalid screen scale");
                return;
            }
            emit peer->screenScale(enabled ? int(value) : 100, revision);
            emit peer->screenScalePreference(int(value), revision, enabled != 0);
            break;
        }
        case FR_POINTER_SPEED: {
            if (payload.size() != 12) {
                fail("Invalid pointer speed");
                return;
            }
            const auto* data = reinterpret_cast<const uint8_t*>(payload.constData());
            const auto percent = fr_get_u32(data);
            const quint64 revision = (quint64(fr_get_u32(data + 4)) << 32) | fr_get_u32(data + 8);
            if (percent < 25 || percent > 400 || revision > quint64(0x7fffffffffffffffULL)) {
                fail("Invalid pointer speed");
                return;
            }
            emit peer->pointerSpeed(int(percent), revision);
            break;
        }
        case FR_PROFILE:
            if (payload.size() != 4 ||
                !fr_resolution_valid(fr_get_u32(reinterpret_cast<const uint8_t*>(payload.constData())))) {
                fail("Invalid video profile");
                return;
            }
            emit peer->profile(int(fr_get_u32(reinterpret_cast<const uint8_t*>(payload.constData()))));
            break;
        case FR_FEEDBACK: {
            if (payload.size() != 16) {
                fail("Invalid video feedback");
                return;
            }
            const auto* data = reinterpret_cast<const uint8_t*>(payload.constData());
            auto fps = fr_get_u32(data), work = fr_get_u32(data + 4), queue = fr_get_u32(data + 8),
                 display = fr_get_u32(data + 12);
            if (fps > 240000 || work > 10000000 || queue > 10000 || display > 240000) {
                fail("Invalid video feedback");
                return;
            }
            emit peer->feedback(fps / 1000.0, work / 1000.0, queue, display / 1000.0);
            break;
        }
        case FR_FRAME: {
            QBuffer buffer;
            buffer.setData(payload);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer, "JPEG");
            const auto size = reader.size();
            if (!size.isValid() || size.width() > 4096 || size.height() > 4096 ||
                qint64(size.width()) * size.height() > 16777216) {
                fail(Peer::tr("Invalid frame dimensions"));
                return;
            }
            auto image = reader.read();
            if (image.isNull()) {
                fail(Peer::tr("Invalid JPEG"));
                return;
            }
            emit peer->frame(image);
            break;
        }
        case FR_GESTURE: {
            fr_gesture gesture{};
            if (!fr_decode_gesture(reinterpret_cast<const uint8_t*>(payload.constData()), payload.size(), &gesture)) {
                fail(Peer::tr("Invalid gesture"));
                return;
            }
            emit peer->gesture(gesture);
            break;
        }
        case FR_KEY:
            if (payload.size() != 4) {
                fail(Peer::tr("Invalid key"));
                return;
            }
            emit peer->key(fr_get_u32(reinterpret_cast<const uint8_t*>(payload.constData())));
            break;
        case FR_STATUS:
            if (payload.size() > 4096) {
                fail(Peer::tr("Invalid status"));
                return;
            }
            if (payload == "Furina capability: screen-scale-v2") {
                if (!screenScaleToggleSupported) {
                    screenScaleSupported = true;
                    screenScaleToggleSupported = true;
                    emit peer->screenScaleAvailable();
                }
                break;
            }
            if (payload == "Furina capability: screen-scale-v1") {
                if (!screenScaleSupported) {
                    screenScaleSupported = true;
                    emit peer->screenScaleAvailable();
                }
                break;
            }
            emit peer->status(QString::fromUtf8(payload));
            break;
        case FR_APPROVAL: {
            if (payload.size() != 12) {
                fail("Invalid approval request");
                return;
            }
            const auto* data = reinterpret_cast<const uint8_t*>(payload.constData());
            const auto id = fr_get_u32(data), verb = fr_get_u32(data + 4), options = fr_get_u32(data + 8);
            if (!fr_valid_approval(id, verb, options)) {
                fail("Invalid approval request");
                return;
            }
            emit peer->approval(id, verb, options);
            break;
        }
        case FR_SESSION: {
            if (payload.size() != 4) {
                fail("Invalid session request");
                return;
            }
            const auto action = fr_get_u32(reinterpret_cast<const uint8_t*>(payload.constData()));
            if (!fr_valid_session_action(action)) {
                fail("Invalid session request");
                return;
            }
            emit peer->session(action);
            break;
        }
        case FR_PING:
            if (!payload.isEmpty()) {
                fail(Peer::tr("Invalid ping"));
                return;
            }
            sendPlain(FR_PONG, {}, false);
            break;
        case FR_PONG:
            if (!payload.isEmpty())
                fail(Peer::tr("Invalid pong"));
            else if (pendingPings > 0)
                --pendingPings;
            break;
        default:
            fail(Peer::tr("Unexpected message"));
        }
    }
};

Peer::Peer(QObject* parent) : QObject(parent) {
    m_io = new PeerIo(this);
    m_io->moveToThread(&m_thread);
    m_thread.setObjectName("furina-peer-io");
    m_thread.start();
}

Peer::~Peer() {
    close(false);
    m_thread.quit();
    m_thread.wait();
    delete m_io;
    m_io = nullptr;
}

bool Peer::listen(quint16 port, const QString& token, const QString& bindAddress) {
    close(false);
    if (token.size() < 6) {
        emit status(tr("Pairing code requires at least 6 characters"));
        return false;
    }
    const auto bytes = token.toUtf8();
    QByteArray longTerm;
    {
        QMutexLocker lock(&m_infoLock);
        longTerm = m_longTermKey;
    }
    if (oneTimeToken(bytes) && longTerm.size() != 32 && oneTimeUsed(bytes)) {
        emit status(oneTimeStatus());
        return false;
    }
    bool ok = false;
    QMetaObject::invokeMethod(
        m_io, [&] { ok = m_io->listen(port, token, bindAddress, longTerm); }, Qt::BlockingQueuedConnection);
    return ok;
}

void Peer::connectTo(const QString& host, quint16 port, const QString& token, const QString& localAddress) {
    close(false);
    if (token.size() < 6) {
        emit status(tr("Enter the tablet pairing code (at least 6 characters)"));
        return;
    }
    const auto bytes = token.toUtf8();
    QByteArray longTerm;
    {
        QMutexLocker lock(&m_infoLock);
        longTerm = m_longTermKey;
    }
    if (oneTimeToken(bytes) && longTerm.size() != 32 && oneTimeUsed(bytes)) {
        emit status(oneTimeStatus());
        emit disconnected();
        return;
    }
    if (!localAddress.isEmpty()) {
        QHostAddress local;
        if (!local.setAddress(localAddress) || local.protocol() != QAbstractSocket::IPv4Protocol) {
            emit status("无法绑定 USB 网卡，连接已取消。\nCould not bind the USB adapter. Connection cancelled.");
            emit disconnected();
            return;
        }
    }
    QMetaObject::invokeMethod(
        m_io,
        [=] { m_io->connectToHost(host, port, token, localAddress, longTerm); }, Qt::QueuedConnection);
}

void Peer::close(bool graceful) {
    if (!m_io)
        return;
    if (QThread::currentThread() == m_io->thread())
        m_io->close(graceful);
    else
        QMetaObject::invokeMethod(m_io, [this, graceful] { m_io->close(graceful); }, Qt::BlockingQueuedConnection);
}

void Peer::setLongTermKey(const QByteArray& key) {
    QMutexLocker lock(&m_infoLock);
    if (key.isEmpty())
        m_longTermKey.clear();
    else if (key.size() == 32)
        m_longTermKey = key;
}

QByteArray Peer::establishedLongTermKey() const {
    QMutexLocker lock(&m_infoLock);
    return m_established;
}

bool Peer::canSendFrame() const {
    return m_ready.load() && m_bytesToWrite.load() < 256 * 1024;
}

void Peer::sendFrame(const QImage& image) {
    if (!m_ready.load() || image.isNull() || !canSendFrame())
        return;
    QMetaObject::invokeMethod(m_io, [this, image] { m_io->sendFrame(image); }, Qt::QueuedConnection);
}

void Peer::sendJpeg(const QByteArray& jpeg) {
    if (!canSendFrame())
        return;
    QMetaObject::invokeMethod(m_io, [this, jpeg] { m_io->sendPlain(FR_FRAME, jpeg, false); }, Qt::QueuedConnection);
}

void Peer::sendGesture(const fr_gesture& gesture) {
    if (!m_ready.load())
        return;
    QMetaObject::invokeMethod(m_io, [this, gesture] {
        uint8_t data[20];
        if (fr_encode_gesture(data, &gesture))
            m_io->sendPlain(FR_GESTURE, QByteArray(reinterpret_cast<const char*>(data), 20), false);
    }, Qt::QueuedConnection);
}

void Peer::sendKey(quint32 key) {
    if (!m_ready.load())
        return;
    QMetaObject::invokeMethod(m_io, [this, key] {
        uint8_t data[4];
        fr_put_u32(data, key);
        m_io->sendPlain(FR_KEY, QByteArray(reinterpret_cast<const char*>(data), 4), false);
    }, Qt::QueuedConnection);
}

void Peer::sendStatus(const QString& text) {
    if (!m_ready.load())
        return;
    QMetaObject::invokeMethod(m_io, [this, text] { m_io->sendPlain(FR_STATUS, text.toUtf8().left(4096), false); },
                              Qt::QueuedConnection);
}

void Peer::sendSession(quint32 action) {
    if (!m_ready.load() || !fr_valid_session_action(action))
        return;
    QMetaObject::invokeMethod(m_io, [this, action] {
        uint8_t data[4];
        fr_put_u32(data, action);
        m_io->sendPlain(FR_SESSION, QByteArray(reinterpret_cast<const char*>(data), 4), false);
    }, Qt::QueuedConnection);
}

void Peer::sendApproval(quint32 id, quint32 verb, quint32 options) {
    if (!m_ready.load() || !fr_valid_approval(id, verb, options))
        return;
    QMetaObject::invokeMethod(m_io, [this, id, verb, options] {
        uint8_t data[12];
        fr_put_u32(data, id);
        fr_put_u32(data + 4, verb);
        fr_put_u32(data + 8, options);
        m_io->sendPlain(FR_APPROVAL, QByteArray(reinterpret_cast<const char*>(data), 12), false);
    }, Qt::QueuedConnection);
}

bool Peer::sendVideo(const QByteArray& packet) {
    fr_video_info info{};
    if (!m_ready.load() ||
        !fr_video_read(reinterpret_cast<const uint8_t*>(packet.constData()), packet.size(), &info))
        return false;
    if (m_bytesToWrite.load() > 128 * 1024 || videoQueueMs() > 33 || m_videoInFlight.load() >= 3) {
        m_needVideoKey.store(true);
        return false;
    }
    if (m_needVideoKey.load() && !(info.flags & FR_VIDEO_KEY))
        return false;
    m_needVideoKey.store(false);
    m_videoInFlight.fetch_add(1);
    QMetaObject::invokeMethod(m_io, [this, packet] { m_io->sendPlain(FR_VIDEO, packet, true); }, Qt::QueuedConnection);
    return true;
}

void Peer::sendProfile(int edge) {
    if (!m_ready.load() || !fr_resolution_valid(edge))
        return;
    QMetaObject::invokeMethod(m_io, [this, edge] {
        uint8_t data[4];
        fr_put_u32(data, uint32_t(edge));
        m_io->sendPlain(FR_PROFILE, QByteArray(reinterpret_cast<const char*>(data), 4), false);
    }, Qt::QueuedConnection);
}

void Peer::sendFeedback(double fps, double workMs, unsigned queueMs, double displayedFps) {
    if (!m_ready.load())
        return;
    QMetaObject::invokeMethod(m_io, [this, fps, workMs, queueMs, displayedFps] {
        uint8_t data[16];
        fr_put_u32(data, uint32_t(qBound(0.0, fps, 240.0) * 1000));
        fr_put_u32(data + 4, uint32_t(qBound(0.0, workMs, 10000.0) * 1000));
        fr_put_u32(data + 8, qMin(10000u, queueMs));
        fr_put_u32(data + 12, uint32_t(qBound(0.0, displayedFps, 240.0) * 1000));
        m_io->sendPlain(FR_FEEDBACK, QByteArray(reinterpret_cast<const char*>(data), 16), false);
    }, Qt::QueuedConnection);
}

void Peer::sendPointerSpeed(int percent, quint64 revision) {
    if (!m_ready.load() || percent < 25 || percent > 400 || revision > quint64(0x7fffffffffffffffULL))
        return;
    QMetaObject::invokeMethod(m_io, [this, percent, revision] {
        uint8_t data[12];
        fr_put_u32(data, uint32_t(percent));
        fr_put_u32(data + 4, uint32_t(revision >> 32));
        fr_put_u32(data + 8, uint32_t(revision));
        m_io->sendPlain(FR_POINTER_SPEED, QByteArray(reinterpret_cast<const char*>(data), 12), false);
    }, Qt::QueuedConnection);
}

void Peer::sendCursorPreference(bool enabled, quint64 revision) {
    if (!m_ready.load() || revision > quint64(INT64_MAX))
        return;
    QMetaObject::invokeMethod(m_io, [this, enabled, revision] {
        uint8_t data[12];
        fr_put_u32(data, enabled ? 1 : 0);
        fr_put_u32(data + 4, uint32_t(revision >> 32));
        fr_put_u32(data + 8, uint32_t(revision));
        m_io->sendPlain(FR_CURSOR_PREFERENCE, QByteArray(reinterpret_cast<const char*>(data), 12), false);
    }, Qt::QueuedConnection);
}

void Peer::sendScreenScale(int percent, quint64 revision, bool enabled) {
    if (!m_ready.load())
        return;
    QMetaObject::invokeMethod(m_io, [this, percent, revision, enabled] {
        if (!m_io->screenScaleSupported || !fr_screen_scale_valid(percent) || revision > quint64(INT64_MAX))
            return;
        uint8_t data[16];
        fr_put_u32(data, m_io->screenScaleToggleSupported || enabled ? uint32_t(percent) : 100u);
        fr_put_u32(data + 4, uint32_t(revision >> 32));
        fr_put_u32(data + 8, uint32_t(revision));
        fr_put_u32(data + 12, enabled ? 1 : 0);
        m_io->sendPlain(FR_SCREEN_SCALE,
                        QByteArray(reinterpret_cast<const char*>(data), m_io->screenScaleToggleSupported ? 16 : 12),
                        false);
    }, Qt::QueuedConnection);
}

void Peer::sendCursor(const QByteArray& data) {
    QMetaObject::invokeMethod(m_io, [this, data] { m_io->cursor.send(data); }, Qt::QueuedConnection);
}

unsigned Peer::videoQueueMs() const {
    if (m_bytesToWrite.load() <= 0)
        return 0;
    const auto start = m_backlogStartMs.load();
    if (start <= 0)
        return 0;
    const auto elapsed = QDateTime::currentMSecsSinceEpoch() - start;
    return elapsed <= 0 ? 0u : unsigned(std::min<qint64>(10000, elapsed));
}

QHostAddress Peer::boundAddress() const {
    QMutexLocker lock(&m_infoLock);
    return m_bound;
}

quint16 Peer::serverPort() const {
    QMutexLocker lock(&m_infoLock);
    return m_port;
}

QObject* Peer::mainTransport() const {
    return m_mainSocket.load();
}
