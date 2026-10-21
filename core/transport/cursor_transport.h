#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QPointer>
#include <QTimer>
#include <QNetworkProxy>
#include <functional>
#include <memory>
#include "cursor_wire.h"

// A second TCP connection uses the same listening port/ADB forward. Its key is
// derived from the already-authenticated main session's fresh challenge.
class CursorTransport {
    QObject* m_owner;
    QPointer<QTcpSocket> m_socket;
    QList<QPointer<QTcpSocket>> m_candidates;
    QByteArray m_key, m_position, m_shape;
    bool m_ready = false, m_server = false, m_shapeDirty = false, m_positionDirty = false;
    QTimer m_retry;
    QHostAddress m_address, m_local;
    quint16 m_port = 0;

    void ready(QTcpSocket* s) {
        // Native socket options cannot be applied before connectToHost creates
        // the client socket; also cover authenticated reconnects here.
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_socket = s;
        m_ready = true;
        m_shapeDirty = !m_shape.isEmpty();
        m_positionDirty = !m_position.isEmpty();
        if (availability)
            availability(true);
        flush();
    }

    void flush() {
        if (!m_ready || !m_socket || m_socket->bytesToWrite() > 4096)
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
        uint8_t h[4];
        fr_put_u32(h, p.size());
        m_socket->write(reinterpret_cast<char*>(h), 4);
        m_socket->write(p);
    }

    void watch(QTcpSocket* s, bool accepted) {
        struct Input {
            QByteArray bytes;
            bool authenticated = false;
        };

        auto in = std::make_shared<Input>();
        m_candidates.append(s);
        s->setParent(m_owner);
        s->setProxy(QNetworkProxy::NoProxy);
        if (accepted)
            s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        s->setReadBufferSize(1024 * 1024 + 64);
        QObject::connect(s, &QTcpSocket::readyRead, m_owner, [this, s, in, accepted] {
            in->bytes += s->readAll();
            if (in->bytes.size() > 1024 * 1024 + 64) {
                s->abort();
                return;
            }
            if (!in->authenticated) {
                int n = accepted ? 32 : 4;
                if (in->bytes.size() < n)
                    return;
                if (m_key.size() != 32) {
                    s->abort();
                    return;
                }
                bool ok = true;
                if (accepted) {
                    unsigned diff = 0;
                    for (int i = 0; i < 32; ++i)
                        diff |= uint8_t(in->bytes[i]) ^ uint8_t(m_key.at(i));
                    ok = ok && !diff && (!m_socket || m_socket == s);
                } else
                    ok = ok && in->bytes.first(4) == "CUR1";
                if (!ok) {
                    s->abort();
                    return;
                }
                in->bytes.remove(0, n);
                in->authenticated = true;
                if (accepted)
                    s->write("CUR1", 4);
                ready(s);
            }
            while (in->bytes.size() >= 4) {
                auto n = fr_get_u32(reinterpret_cast<const uint8_t*>(in->bytes.constData()));
                if (n < 4 || n > 1024 * 1024 + 16) {
                    s->abort();
                    return;
                }
                if (in->bytes.size() < 4 + int(n))
                    break;
                auto p = in->bytes.mid(4, n);
                in->bytes.remove(0, 4 + n);
                if (!fr_cursor_packet_valid(p)) {
                    s->abort();
                    return;
                }
                if (packet)
                    packet(p);
            }
        });
        QObject::connect(s, &QTcpSocket::bytesWritten, m_owner, [this] { flush(); });
        QObject::connect(s, &QTcpSocket::disconnected, m_owner, [this, s] {
            m_candidates.removeAll(s);
            if (m_socket == s) {
                m_socket = nullptr;
                m_ready = false;
                if (availability)
                    availability(false);
            }
            s->deleteLater();
            if (!m_server && !m_key.isEmpty() && !m_retry.isActive())
                m_retry.start();
        });
        QObject::connect(s, &QTcpSocket::errorOccurred, m_owner, [this, s](QAbstractSocket::SocketError) {
            s->abort();
            if (s->state() == QAbstractSocket::UnconnectedState) {
                m_candidates.removeAll(s);
                if (m_socket == s) {
                    m_socket = nullptr;
                    m_ready = false;
                    if (availability)
                        availability(false);
                }
                s->deleteLater();
                if (!m_server && !m_key.isEmpty() && !m_retry.isActive())
                    m_retry.start();
            }
        });
        QTimer::singleShot(2500, s, [s, in] {
            if (!in->authenticated)
                s->abort();
        });
        if (!accepted)
            QObject::connect(s, &QTcpSocket::connected, m_owner, [this, s] {
                s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
                s->write(m_key);
            });
    }

    void connectClient() {
        if (m_key.isEmpty() || m_server || m_socket)
            return;
        auto* s = new QTcpSocket(m_owner);
        m_socket = s;
        watch(s, false);
        if (!m_local.isNull() && !s->bind(m_local, 0)) {
            s->abort();
            m_socket = nullptr;
            s->deleteLater();
            m_retry.start();
            return;
        }
        s->connectToHost(m_address, m_port);
    }

  public:
    std::function<void(bool)> availability;
    std::function<void(const QByteArray&)> packet;

    explicit CursorTransport(QObject* owner) : m_owner(owner) {
        m_retry.setSingleShot(true);
        m_retry.setInterval(1000);
        QObject::connect(&m_retry, &QTimer::timeout, owner, [this] { connectClient(); });
    }

    ~CursorTransport() {
        close();
    }

    bool isReady() const {
        return m_ready;
    }

    void close() {
        m_key.clear();
        m_retry.stop();
        const bool was = m_ready;
        m_ready = false;
        m_socket = nullptr;
        auto pending = m_candidates;
        m_candidates.clear();
        for (auto s : pending)
            if (s) {
                s->disconnect(m_owner);
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
        if (!server)
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
