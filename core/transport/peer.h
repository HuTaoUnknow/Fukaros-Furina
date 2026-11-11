#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QTcpServer>
#include <QTimer>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QImage>
#include <QMutex>
#include <QThread>
#include <atomic>
#include "furina_protocol.h"
#include "cursor_transport.h"

class PeerIo;

class Peer : public QObject {
    Q_OBJECT
    friend class PeerIo;

  public:
    explicit Peer(QObject* parent = nullptr);
    ~Peer() override;
    // token is a reusable human pairing code, or a one-time QR secret "ot1." plus
    // 22 unpadded base64url characters. setLongTermKey() overrides token for SPAKE2.
    bool listen(quint16 port, const QString& token, const QString& bindAddress = QString());
    void connectTo(const QString& host, quint16 port, const QString& token,
                   const QString& localAddress = QString());
    // Finish queued session cancellation before closing an intentional stop.
    void close(bool graceful = false);
    // 32-byte key from longTermKeyEstablished. Empty clears it. Later connections
    // authenticate with this key instead of token.
    void setLongTermKey(const QByteArray& key);
    QByteArray establishedLongTermKey() const;

    bool ready() const {
        return m_ready.load();
    }

    bool canSendFrame() const;
    void sendFrame(const QImage& image);
    void sendJpeg(const QByteArray& jpeg);
    void sendGesture(const fr_gesture& g);
    void sendKey(quint32 key);
    void sendStatus(const QString& text);
    void sendSession(quint32 action);
    void sendApproval(quint32 id, quint32 verb, quint32 options);
    bool sendVideo(const QByteArray& packet);
    void sendProfile(int edge);
    void sendScreenScale(int percent, quint64 revision, bool enabled = true);
    void sendPointerSpeed(int percent, quint64 revision);
    void sendCursorPreference(bool enabled, quint64 revision);
    void sendCursor(const QByteArray& data);

    bool cursorReady() const {
        return m_cursorReady.load();
    }

    void sendFeedback(double fps, double workMs, unsigned queueMs, double displayedFps);
    unsigned videoQueueMs() const;
    QHostAddress boundAddress() const;
    quint16 serverPort() const;
    QObject* mainTransport() const;
    int videoInFlight() const {
        return m_videoInFlight.load();
    }
    bool hasOpenSockets() const {
        return m_openSockets.load() != 0;
    }

  signals:
    void connected();
    void disconnected();
    void status(const QString& message);
    void frame(const QImage& image);
    void gesture(fr_gesture g);
    void key(quint32 key);
    void session(quint32 action);
    void approval(quint32 id, quint32 verb, quint32 options);
    void video(const QByteArray& packet);
    void profile(int edge);
    void screenScaleAvailable();
    void screenScale(int percent, quint64 revision);
    void screenScalePreference(int percent, quint64 revision, bool enabled);
    void pointerSpeed(int percent, quint64 revision);
    void cursorPreference(bool enabled, quint64 revision);
    void cursorConnection(bool ready);
    void cursor(const QByteArray& data);
    void feedback(double fps, double workMs, unsigned queueMs, double displayedFps);
    void longTermKeyEstablished(const QByteArray& key);

  private:
    QThread m_thread;
    PeerIo* m_io = nullptr;
    mutable QMutex m_infoLock;
    QByteArray m_longTermKey;
    QByteArray m_established;
    QHostAddress m_bound;
    quint16 m_port = 0;
    std::atomic<QTcpSocket*> m_mainSocket{nullptr};
    std::atomic<bool> m_ready{false};
    std::atomic<bool> m_cursorReady{false};
    std::atomic<int> m_videoInFlight{0};
    std::atomic<bool> m_needVideoKey{false};
    std::atomic<qint64> m_bytesToWrite{0};
    std::atomic<qint64> m_backlogStartMs{0};
    std::atomic<int> m_openSockets{0};
};
