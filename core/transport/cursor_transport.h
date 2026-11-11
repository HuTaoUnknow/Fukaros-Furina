#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QPointer>
#include <QTimer>
#include <QNetworkProxy>
#include <functional>
#include <memory>
#include "cursor_wire.h"
#include "furina_channel.h"

// Second TCP connection on the same listening port. It authenticates with the
// main session's cursor key and a fresh X25519 ephemeral, then carries cursor
// packets inside their own AEAD stream. Reconnect starts a new ephemeral and
// new counters so a dead nonce space is never reused.
class CursorTransport {
    struct Input {
        CursorChannel channel;
    };

    QObject* m_owner;
    QPointer<QTcpSocket> m_socket;
    QList<QPointer<QTcpSocket>> m_candidates;
    std::shared_ptr<Input> m_live;
    QByteArray m_key, m_position, m_shape;
    bool m_ready = false, m_server = false, m_shapeDirty = false, m_positionDirty = false;
    QTimer* m_retry = nullptr;
    QHostAddress m_address, m_local;
    quint16 m_port = 0;

    void ready(QTcpSocket* s, const std::shared_ptr<Input>& in) {
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_socket = s;
        m_live = in;
        m_ready = true;
        m_shapeDirty = !m_shape.isEmpty();
        m_positionDirty = !m_position.isEmpty();
        if (availability)
            availability(true);
        flush();
    }

    void flush() {
        if (!m_ready || !m_socket || !m_live || m_socket->bytesToWrite() > 4096)
            return;
        if (m_shapeDirty) {
            write(m_shape);
            m_shapeDirty = false;
        }
        if (m_positionDirty) {
            write(m_position);
            m_positionDirty = false;
        }
    }

    void write(const QByteArray& p) {
        if (!m_live)
            return;
        QByteArray wire;
        if (!m_live->channel.seal(p, &wire)) {
            if (m_socket)
                m_socket->abort();
            return;
        }
        m_socket->write(wire);
    }

    void watch(QTcpSocket* s, bool accepted) {
        auto in = std::make_shared<Input>();
        in->channel.reset(!accepted, m_key);
        m_candidates.append(s);
        s->setParent(m_owner);
        s->setProxy(QNetworkProxy::NoProxy);
        if (accepted)
            s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        s->setReadBufferSize(1024 * 1024 + 64);
        if (accounting)
            accounting(1);
        QObject::connect(s, &QObject::destroyed, m_owner, [this] {
            if (accounting)
                accounting(-1);
        });
        QObject::connect(s, &QTcpSocket::readyRead, m_owner, [this, s, in, accepted] {
            if (m_ready && m_socket && m_socket != s) {
                s->abort();
                return;
            }
            QByteArray egress;
            if (!in->channel.push(s->readAll(), &egress)) {
                s->abort();
                return;
            }
            if (!egress.isEmpty())
                s->write(egress);
            if (in->channel.ready() && !in->channel.error().isEmpty()) {
                s->abort();
                return;
            }
            if (in->channel.ready() && (!m_socket || m_socket == s) && m_live != in)
                ready(s, in);
            QByteArray packet;
            while (in->channel.pop(&packet)) {
                if (!fr_cursor_packet_valid(packet)) {
                    s->abort();
                    return;
                }
                if (this->packet)
                    this->packet(packet);
            }
            (void)accepted;
        });
        QObject::connect(s, &QTcpSocket::bytesWritten, m_owner, [this] { flush(); });
        QObject::connect(s, &QTcpSocket::disconnected, m_owner, [this, s] {
            m_candidates.removeAll(s);
            if (m_socket == s) {
                m_socket = nullptr;
                m_live.reset();
                m_ready = false;
                if (availability)
                    availability(false);
            }
            s->deleteLater();
            if (!m_server && !m_key.isEmpty() && m_retry && !m_retry->isActive())
                m_retry->start();
        });
        QObject::connect(s, &QTcpSocket::errorOccurred, m_owner, [this, s](QAbstractSocket::SocketError) {
            s->abort();
            if (s->state() == QAbstractSocket::UnconnectedState) {
                m_candidates.removeAll(s);
                if (m_socket == s) {
                    m_socket = nullptr;
                    m_live.reset();
                    m_ready = false;
                    if (availability)
                        availability(false);
                }
                s->deleteLater();
                if (!m_server && !m_key.isEmpty() && m_retry && !m_retry->isActive())
                    m_retry->start();
            }
        });
        QTimer::singleShot(2500, s, [s, in] {
            if (!in->channel.ready())
                s->abort();
        });
        if (accepted) {
            const auto hello = in->channel.start();
            if (hello.isEmpty())
                s->abort();
            else
                s->write(hello);
        } else {
            QObject::connect(s, &QTcpSocket::connected, m_owner, [s, in] {
                s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
                const auto hello = in->channel.start();
                if (hello.isEmpty())
                    s->abort();
                else
                    s->write(hello);
            });
        }
    }

    void connectClient() {
        if (m_key.size() != 32 || m_server || m_socket)
            return;
        auto* s = new QTcpSocket(m_owner);
        m_socket = s;
        watch(s, false);
        if (!m_local.isNull() && !s->bind(m_local, 0)) {
            s->abort();
            m_socket = nullptr;
            s->deleteLater();
            if (m_retry)
                m_retry->start();
            return;
        }
        s->connectToHost(m_address, m_port);
    }

  public:
    std::function<void(bool)> availability;
    std::function<void(const QByteArray&)> packet;
    std::function<void(int)> accounting;

    explicit CursorTransport(QObject* owner) : m_owner(owner), m_retry(new QTimer(owner)) {
        m_retry->setSingleShot(true);
        m_retry->setInterval(1000);
        QObject::connect(m_retry, &QTimer::timeout, owner, [this] { connectClient(); });
    }

    ~CursorTransport() {
        close();
    }

    bool isReady() const {
        return m_ready;
    }

    void close() {
        if (!m_key.isEmpty()) {
            m_key.fill(0);
            m_key.clear();
        }
        if (m_live)
            m_live->channel.wipe();
        m_live.reset();
        if (m_retry)
            m_retry->stop();
        const bool was = m_ready;
        m_ready = false;
        m_socket = nullptr;
        auto pending = m_candidates;
        m_candidates.clear();
        for (auto s : pending)
            if (s) {
                s->disconnect(m_owner);
                if (accounting)
                    accounting(-1);
                s->abort();
                s->deleteLater();
            }
        m_position.clear();
        m_shape.clear();
        m_shapeDirty = m_positionDirty = false;
        if (was && availability)
            availability(false);
    }

    void begin(const QByteArray& key, bool server, const QHostAddress& address, quint16 port,
               const QHostAddress& local = {}) {
        close();
        m_key = key;
        m_server = server;
        m_address = address;
        m_port = port;
        m_local = local;
        if (!server && m_key.size() == 32)
            connectClient();
    }

    void accept(QTcpSocket* s) {
        if (!m_server || m_key.size() != 32 || m_ready || m_candidates.size() >= 2) {
            s->abort();
            s->deleteLater();
            return;
        }
        watch(s, true);
    }

    void send(const QByteArray& p) {
        if (!fr_cursor_packet_valid(p))
            return;
        if (fr_get_u32(reinterpret_cast<const uint8_t*>(p.constData())) == 2) {
            m_shape = p;
            m_shapeDirty = true;
        } else {
            m_position = p;
            m_positionDirty = true;
        }
        flush();
    }
};
