#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QTcpServer>
#include <QTimer>
#include <QElapsedTimer>
#include <QImage>
#include "furina_protocol.h"
#include "cursor_transport.h"

class Peer : public QObject {
    Q_OBJECT
  public:
    explicit Peer(QObject* parent = nullptr);
    ~Peer() override;
    bool listen(quint16 port, const QString& token, const QString& bindAddress = QString());
    void connectTo(const QString& host, quint16 port, const QString& token,
                   const QString& localAddress = QString());
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
    QByteArray m_rx, m_challenge;
    QString m_token;
    bool m_serverSide = false, m_ready = false, m_waitVideoKey = false;
    int m_pendingPings = 0;
    void attach(QTcpSocket* socket, bool server);
    void startCursor();
    void read();
    void handle(quint8 type, const QByteArray& payload);
    void send(quint8 type, const QByteArray& payload = {});
    void fail(const QString& reason);
};
