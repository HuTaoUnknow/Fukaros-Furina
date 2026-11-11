#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QTcpServer>
#include <QTimer>
#include <QElapsedTimer>
#include <QImage>
#include "furina_protocol.h"
#include "furina_security.h"
#include "cursor_transport.h"

class Peer : public QObject {
    Q_OBJECT
  public:
    explicit Peer(QObject* parent = nullptr);
    ~Peer() override;
    bool listen(quint16 port, const QString& token, const QString& bindAddress = QString());
    void connectTo(const QString& host, quint16 port, const QString& token,
                   const QString& localAddress = QString());
    // expiresAtMs is Unix milliseconds; 0 disables expiry. A QR offer is single-use.
    void setOfferWindow(qint64 expiresAtMs, bool singleUse);
    void setExpectedPeer(const QByteArray& publicKey);
    void useStoredIdentity();
    QByteArray localPublicKey();
    QByteArray peerPublicKey() const;
    // Finish queued session cancellation before closing an intentional stop.
    void close(bool graceful = false);

    bool ready() const {
        return m_ready;
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

    void sendCursor(const QByteArray& data) {
        m_cursor.send(data);
    }

    bool cursorReady() const {
        return m_cursor.isReady();
    }

    void sendFeedback(double fps, double workMs, unsigned queueMs, double displayedFps);
    unsigned videoQueueMs() const;
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

  private:
    CursorTransport m_cursor;
    QTcpServer m_server;
    QTcpSocket* m_socket = nullptr;
    QTimer m_tick;
    QElapsedTimer m_lastRx, m_session, m_backlog;
    QByteArray m_rx, m_stream, m_psk, m_expectedPeer, m_sessionPeer;
    fr_identity m_identity{};
    fr_handshake m_hs{};
    fr_attempt_gate m_gate{};
    uint8_t m_sendKey[32]{}, m_recvKey[32]{}, m_cursorKey[32]{}, m_sendSalt[16]{}, m_recvSalt[16]{};
    uint64_t m_sendCounter = 0, m_recvCounter = 0;
    qint64 m_expiresAtMs = 0;
    bool m_haveIdentity = false, m_storeIdentity = false, m_secure = false, m_singleUse = false,
         m_offerUsed = false;
    enum Handshake { HsIdle, HsWaitHello, HsWaitServer, HsWaitConfirm, HsOpen } m_phase = HsIdle;
    bool m_screenScaleSupported = false;
    bool m_screenScaleToggleSupported = false;
    bool m_serverSide = false, m_ready = false, m_waitVideoKey = false;
    int m_pendingPings = 0;
    void attach(QTcpSocket* socket, bool server);
    void startCursor();
    void read();
    void handle(quint8 type, const QByteArray& payload);
    void send(quint8 type, const QByteArray& payload = {});
    void fail(const QString& reason);
    void ensureIdentity();
    bool takeCredential(const QString& token, bool listening);
    void beginClient();
    bool pumpHandshake();
    void acceptSecure(bool serverReady);
    void rememberOffer(qint64* expiry, bool* singleUse, QByteArray* expected);
    void restoreOffer(qint64 expiry, bool singleUse, const QByteArray& expected);
};
