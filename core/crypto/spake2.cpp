#include "spake2.h"
#include "ed25519_group.h"
#include "hkdf.h"
#include "monocypher.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <cstring>

namespace {
/* RFC 9382 edwards25519 M and N. */
const uint8_t kM[32] = {0xd0, 0x48, 0x03, 0x2c, 0x6e, 0xa0, 0xb6, 0xd6, 0x97, 0xdd, 0xc2, 0xe8, 0x6b, 0xda, 0x85, 0xa3,
                        0x3a, 0xda, 0xc9, 0x20, 0xf1, 0xbf, 0x18, 0xe1, 0xb0, 0xc6, 0xd1, 0x66, 0xa5, 0xce, 0xcd, 0xaf};
const uint8_t kN[32] = {0xd3, 0xbf, 0xb5, 0x18, 0xf4, 0x4f, 0x34, 0x30, 0xf2, 0x9d, 0x0c, 0x92, 0xaf, 0x50, 0x38, 0x65,
                        0xa1, 0xed, 0x32, 0x81, 0xdc, 0x69, 0xb3, 0x5d, 0xd8, 0x68, 0xba, 0x85, 0xf8, 0x86, 0xc4, 0xab};
const QByteArray kIdA = QByteArray("furina-client");
const QByteArray kIdB = QByteArray("furina-server");
const QByteArray kAad = QByteArray("FUR2-2");

QByteArray le64(quint64 n) {
    QByteArray out(8, 0);
    for (int i = 0; i < 8; ++i)
        out[i] = char((n >> (8 * i)) & 0xff);
    return out;
}

QByteArray be32(const uint8_t le[32]) {
    QByteArray out(32, 0);
    for (int i = 0; i < 32; ++i)
        out[31 - i] = char(le[i]);
    return out;
}

void appendLen(QByteArray* transcript, const QByteArray& bytes) {
    transcript->append(le64(quint64(bytes.size())));
    transcript->append(bytes);
}

bool hashPassword(const QByteArray& password, uint8_t w[32]) {
    const auto digest =
        QCryptographicHash::hash(QByteArray("FUR2-SPAKE2-w-v2") + password, QCryptographicHash::Sha512);
    sc_reduce512(w, reinterpret_cast<const uint8_t*>(digest.constData()));
    return !sc_is_zero(w);
}

bool randomScalar(uint8_t scalar[32]) {
    uint8_t wide[64];
    for (int i = 0; i < 64; i += 4) {
        const quint32 r = QRandomGenerator::system()->generate();
        std::memcpy(wide + i, &r, 4);
    }
    sc_reduce512(scalar, wide);
    crypto_wipe(wide, sizeof wide);
    return !sc_is_zero(scalar);
}

bool blindedShare(const uint8_t scalar[32], const uint8_t w[32], const uint8_t* constant, uint8_t out[32]) {
    uint8_t base[32], xb[32], wc[32];
    ed_basepoint(base);
    if (!ed_scalarmult(xb, scalar, base) || !ed_scalarmult(wc, w, constant))
        return false;
    return ed_add(out, xb, wc) == 1;
}

struct Derived {
    uint8_t k[32]{};
    uint8_t session[32]{};
    uint8_t confirmA[32]{};
    uint8_t confirmB[32]{};
};

bool derive(const uint8_t pa[32], const uint8_t pb[32], const uint8_t kPoint[32], const uint8_t w[32], Derived* out) {
    QByteArray tt;
    appendLen(&tt, kIdA);
    appendLen(&tt, kIdB);
    appendLen(&tt, QByteArray(reinterpret_cast<const char*>(pa), 32));
    appendLen(&tt, QByteArray(reinterpret_cast<const char*>(pb), 32));
    appendLen(&tt, QByteArray(reinterpret_cast<const char*>(kPoint), 32));
    appendLen(&tt, be32(w));
    const auto hashed = QCryptographicHash::hash(tt, QCryptographicHash::Sha256);
    std::memcpy(out->session, hashed.constData(), 32);
    std::memcpy(out->k, kPoint, 32);
    const QByteArray ka = hashed.right(16);
    const auto keys = furinaHkdf(QByteArray(), ka, QByteArray("ConfirmationKeys") + kAad, 32);
    const auto cA = furinaHmacSha256(keys.left(16), tt);
    const auto cB = furinaHmacSha256(keys.mid(16, 16), tt);
    std::memcpy(out->confirmA, cA.constData(), 32);
    std::memcpy(out->confirmB, cB.constData(), 32);
    return true;
}

bool sharedElement(bool client, const uint8_t scalar[32], const uint8_t w[32], const uint8_t peer[32],
                   uint8_t kPoint[32]) {
    uint8_t blinded[32], diff[32], dh[32];
    const uint8_t* which = client ? kN : kM;
    if (!ed_scalarmult(blinded, w, which) || !ed_sub(diff, peer, blinded) || !ed_scalarmult(dh, scalar, diff))
        return false;
    uint8_t eight[32] = {8};
    return ed_scalarmult(kPoint, eight, dh) == 1;
}
}

Spake2Party::Spake2Party(bool client, const QByteArray& password) : m_client(client) {
    if (!hashPassword(password, m_w) || !randomScalar(m_scalar))
        return;
    if (!blindedShare(m_scalar, m_w, client ? kM : kN, m_share))
        return;
    m_valid = true;
}

Spake2Party::Spake2Party(Spake2Party&& other) noexcept {
    *this = std::move(other);
}

Spake2Party& Spake2Party::operator=(Spake2Party&& other) noexcept {
    if (this != &other) {
        wipe();
        m_client = other.m_client;
        m_valid = other.m_valid;
        m_finished = other.m_finished;
        std::memcpy(m_scalar, other.m_scalar, 32);
        std::memcpy(m_w, other.m_w, 32);
        std::memcpy(m_share, other.m_share, 32);
        std::memcpy(m_confirm, other.m_confirm, 32);
        std::memcpy(m_peerConfirm, other.m_peerConfirm, 32);
        std::memcpy(m_session, other.m_session, 32);
        other.wipe();
    }
    return *this;
}

Spake2Party::~Spake2Party() {
    wipe();
}

void Spake2Party::wipe() {
    crypto_wipe(m_scalar, sizeof m_scalar);
    crypto_wipe(m_w, sizeof m_w);
    crypto_wipe(m_share, sizeof m_share);
    crypto_wipe(m_confirm, sizeof m_confirm);
    crypto_wipe(m_peerConfirm, sizeof m_peerConfirm);
    crypto_wipe(m_session, sizeof m_session);
    m_valid = m_finished = false;
}

QByteArray Spake2Party::share() const {
    return m_valid ? QByteArray(reinterpret_cast<const char*>(m_share), 32) : QByteArray();
}

bool Spake2Party::finish(const QByteArray& peerShare) {
    if (!m_valid || m_finished || peerShare.size() != 32 || !ed_point_valid(reinterpret_cast<const uint8_t*>(peerShare.constData())))
        return false;
    uint8_t kPoint[32];
    if (!sharedElement(m_client, m_scalar, m_w, reinterpret_cast<const uint8_t*>(peerShare.constData()), kPoint))
        return false;
    const uint8_t* pa = m_client ? m_share : reinterpret_cast<const uint8_t*>(peerShare.constData());
    const uint8_t* pb = m_client ? reinterpret_cast<const uint8_t*>(peerShare.constData()) : m_share;
    Derived derived{};
    if (!derive(pa, pb, kPoint, m_w, &derived))
        return false;
    std::memcpy(m_session, derived.session, 32);
    if (m_client) {
        std::memcpy(m_confirm, derived.confirmA, 32);
        std::memcpy(m_peerConfirm, derived.confirmB, 32);
    } else {
        std::memcpy(m_confirm, derived.confirmB, 32);
        std::memcpy(m_peerConfirm, derived.confirmA, 32);
    }
    crypto_wipe(m_scalar, sizeof m_scalar);
    crypto_wipe(m_w, sizeof m_w);
    crypto_wipe(kPoint, sizeof kPoint);
    m_finished = true;
    return true;
}

QByteArray Spake2Party::confirmation() const {
    return m_finished ? QByteArray(reinterpret_cast<const char*>(m_confirm), 32) : QByteArray();
}

QByteArray Spake2Party::peerConfirmation() const {
    return m_finished ? QByteArray(reinterpret_cast<const char*>(m_peerConfirm), 32) : QByteArray();
}

QByteArray Spake2Party::sessionHash() const {
    return m_finished ? QByteArray(reinterpret_cast<const char*>(m_session), 32) : QByteArray();
}

bool spake2TestVector(const QByteArray& password, const uint8_t clientScalar[32], const uint8_t serverScalar[32],
                      uint8_t elementK[32], uint8_t confirmA[32], uint8_t confirmB[32]) {
    uint8_t w[32], pa[32], pb[32], kClient[32], kServer[32];
    if (!hashPassword(password, w))
        return false;
    if (!blindedShare(clientScalar, w, kM, pa) || !blindedShare(serverScalar, w, kN, pb))
        return false;
    if (!sharedElement(true, clientScalar, w, pb, kClient) || !sharedElement(false, serverScalar, w, pa, kServer))
        return false;
    if (std::memcmp(kClient, kServer, 32) != 0)
        return false;
    Derived derived{};
    if (!derive(pa, pb, kClient, w, &derived))
        return false;
    std::memcpy(elementK, kClient, 32);
    std::memcpy(confirmA, derived.confirmA, 32);
    std::memcpy(confirmB, derived.confirmB, 32);
    crypto_wipe(w, sizeof w);
    return true;
}
