#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QPointer>
#include <QTimer>
#include <QNetworkProxy>
#include <functional>
#include <memory>
#include "cursor_wire.h"
#include "furina_security.h"

// A second TCP connection uses the same listening port/ADB forward. Its key is
// derived from the already-authenticated main session's fresh challenge.
class CursorTransport {
    QObject* m_owner;
    QPointer<QTcpSocket> m_socket;
    QList<QPointer<QTcpSocket>> m_candidates;
    QByteArray m_key, m_position, m_shape;
    bool m_ready = false, m_server = false, m_shapeDirty = false, m_positionDirty = false;
    uint64_t m_sendCount = 0;
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

    bool seal(QTcpSocket* socket, uint8_t direction, uint64_t* counter, const QByteArray& plain) {
        if (!socket || m_key.size() != 32 || plain.size() > 1024 * 1024 + 16)
            return false;
        QByteArray out(plain.size() + 20, Qt::Uninitialized);
        size_t n = 0;
        if (fr_cursor_seal(reinterpret_cast<const uint8_t*>(m_key.constData()), direction, counter,
                           reinterpret_cast<const uint8_t*>(plain.constData()), size_t(plain.size()),
                           reinterpret_cast<uint8_t*>(out.data()), size_t(out.size()), &n) != 1)
            return false;
        return socket->write(out.constData(), qsizetype(n)) == qsizetype(n);
    }

    void write(const QByteArray& p) {
        seal(m_socket, m_server ? 2 : 1, &m_sendCount, p);
    }

    void watch(QTcpSocket* s, bool accepted) {
        struct Input {
            QByteArray bytes;
            bool authenticated = false;
            uint64_t recvCount = 0;
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
            auto openNext = [&](QByteArray& plain) {
                plain.clear();
                if (m_key.size() != 32 || in->bytes.size() < 4)
                    return 0;
                const auto declared = fr_get_u32(reinterpret_cast<const uint8_t*>(in->bytes.constData()));
                if (declared > 1024 * 1024 + 16) {
                    s->abort();
                    return -1;
                }
                plain.resize(int(declared));
                size_t consumed = 0, plain_n = 0;
                const int rc = fr_cursor_open(reinterpret_cast<const uint8_t*>(m_key.constData()), accepted ? 1 : 2,
                                              &in->recvCount, reinterpret_cast<const uint8_t*>(in->bytes.constData()),
                                              size_t(in->bytes.size()), &consumed,
                                              reinterpret_cast<uint8_t*>(plain.data()), size_t(plain.size()), &plain_n);
                if (rc == 1) {
                    in->bytes.remove(0, int(consumed));
                    plain.resize(int(plain_n));
                }
                return rc;
            };
            if (!in->authenticated) {
                QByteArray plain;
                const int rc = openNext(plain);
                if (rc == 0)
                    return;
                const auto expect = accepted ? QByteArray("CUR2") : QByteArray("CUR1");
                if (rc < 0 || plain != expect || (accepted && m_socket && m_socket != s)) {
                    s->abort();
                    return;
                }
                in->authenticated = true;
                if (accepted && !seal(s, 2, &m_sendCount, QByteArray("CUR1"))) {
                    s->abort();
                    return;
                }
                ready(s);
            }
            while (in->authenticated && in->bytes.size() >= 4) {
                QByteArray plain;
                const int rc = openNext(plain);
                if (rc == 0)
                    break;
                if (rc < 0 || !fr_cursor_packet_valid(plain)) {
                    s->abort();
                    return;
                }
                if (packet)
                    packet(plain);
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
                if (!seal(s, 1, &m_sendCount, QByteArray("CUR2")))
                    s->abort();
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
        m_sendCount = 0;
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
