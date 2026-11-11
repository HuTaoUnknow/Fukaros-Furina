#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <cstdint>

/* FUR2 handshake and the AEAD record layer that carries FUR1 frames.
   A failed push() wipes every key from that attempt. Counters start at zero
   on each reset(); a new connection must construct or reset, never reuse. */
class FurinaChannel {
  public:
    enum class Role { Client, Server };

    FurinaChannel();
    ~FurinaChannel();
    FurinaChannel(const FurinaChannel&) = delete;
    FurinaChannel& operator=(const FurinaChannel&) = delete;

    /* existingLongTermKey is either empty or 32 bytes. When set it is the SPAKE2 password. */
    void reset(Role role, const QByteArray& password, const QByteArray& existingLongTermKey = {});
    QByteArray start();
    bool push(const QByteArray& chunk, QByteArray* egress);
    bool transportReady() const {
        return m_ready;
    }
    bool seal(quint8 type, const QByteArray& payload, QByteArray* wire);
    bool pop(quint8* type, QByteArray* payload);
    QByteArray cursorSecret() const;
    QByteArray longTermKey() const;
    QString error() const {
        return m_error;
    }
    void wipe();

  private:
    struct Frame {
        quint8 type;
        QByteArray payload;
    };

    bool onMessage(quint8 type, const QByteArray& body, QByteArray* egress);
    bool parseRecords();
    bool deriveKeys();
    void fail(const QString& reason);
    QByteArray wrapLtk(bool ack) const;
    bool unwrapLtk(const QByteArray& body, bool ack, QByteArray* plain) const;

    Role m_role = Role::Client;
    bool m_started = false;
    bool m_haveBanner = false;
    bool m_haveShare = false;
    bool m_haveConfirm = false;
    bool m_ready = false;
    bool m_presetKey = false;
    class Spake2Party* m_spake = nullptr;
    QByteArray m_in;
    QList<Frame> m_frames;
    QString m_error;
    uint8_t m_sendKey[32]{};
    uint8_t m_recvKey[32]{};
    uint8_t m_ltkKey[32]{};
    uint8_t m_cursor[32]{};
    uint8_t m_ltk[32]{};
    quint64 m_sendCounter = 0;
    quint64 m_recvCounter = 0;
};

/* Second TCP channel. The session secret is the main handshake's cursor key.
   Each reset() uses a fresh X25519 ephemeral, so AEAD nonces are not reused. */
class CursorChannel {
  public:
    CursorChannel();
    ~CursorChannel();
    CursorChannel(const CursorChannel&) = delete;
    CursorChannel& operator=(const CursorChannel&) = delete;

    void reset(bool client, const QByteArray& sessionSecret);
    QByteArray start();
    bool push(const QByteArray& chunk, QByteArray* egress);
    bool ready() const {
        return m_ready;
    }
    bool seal(const QByteArray& packet, QByteArray* wire);
    bool pop(QByteArray* packet);
    QString error() const {
        return m_error;
    }
    void wipe();

  private:
    bool onMessage(quint8 type, const QByteArray& body, QByteArray* egress);
    bool parseRecords();
    void fail(const QString& reason);

    bool m_client = false;
    bool m_started = false;
    bool m_haveBanner = false;
    bool m_haveEph = false;
    bool m_ready = false;
    QByteArray m_in;
    QList<QByteArray> m_packets;
    QString m_error;
    QByteArray m_secret;
    uint8_t m_sk[32]{};
    uint8_t m_pk[32]{};
    uint8_t m_peer[32]{};
    uint8_t m_sendKey[32]{};
    uint8_t m_recvKey[32]{};
    uint8_t m_expect[32]{};
    quint64 m_sendCounter = 0;
    quint64 m_recvCounter = 0;
};

/* Desktop X25519 identity. The secret stays in the pairing store.
   pairingDesktopPublicKey publishes only this 32-byte public key. */
QByteArray furinaX25519Secret();
QByteArray furinaX25519PublicKey(const QByteArray& secretKey);
