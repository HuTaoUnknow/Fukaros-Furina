#pragma once
#include <QByteArray>
#include <QString>
#include <cstdint>

/* SPAKE2 (RFC 9382) on edwards25519 with SHA-256, HKDF, and HMAC.
   A is the TCP client ("furina-client"); B is the TCP server ("furina-server"). */
class Spake2Party {
  public:
    Spake2Party() = default;
    Spake2Party(bool client, const QByteArray& password);
    Spake2Party(const Spake2Party&) = delete;
    Spake2Party& operator=(const Spake2Party&) = delete;
    Spake2Party(Spake2Party&& other) noexcept;
    Spake2Party& operator=(Spake2Party&& other) noexcept;
    ~Spake2Party();

    bool valid() const {
        return m_valid;
    }

    QByteArray share() const;
    /* peerShare is the other side's 32-byte element. */
    bool finish(const QByteArray& peerShare);
    QByteArray confirmation() const;
    QByteArray peerConfirmation() const;
    /* Ke || Ka from Hash(TT), 32 bytes. Empty until finish(). */
    QByteArray sessionHash() const;
    void wipe();

  private:
    bool m_client = false;
    bool m_valid = false;
    bool m_finished = false;
    uint8_t m_scalar[32]{};
    uint8_t m_w[32]{};
    uint8_t m_share[32]{};
    uint8_t m_confirm[32]{};
    uint8_t m_peerConfirm[32]{};
    uint8_t m_session[32]{};
};

/* Fixed-scalar evaluation used by the RFC-style unit vector. Returns false on a bad point. */
bool spake2TestVector(const QByteArray& password, const uint8_t clientScalar[32], const uint8_t serverScalar[32],
                      uint8_t elementK[32], uint8_t confirmA[32], uint8_t confirmB[32]);
