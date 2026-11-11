#include "furina_channel.h"
#include "hkdf.h"
#include "monocypher.h"
#include "spake2.h"
#include "furina_protocol.h"
#include <QRandomGenerator>
#include <cstring>

namespace {
constexpr int kTag = 16;
constexpr int kHeader = 13;
constexpr int kMaxMessage = 128;

void putBe64(QByteArray* out, int offset, quint64 n) {
    for (int i = 0; i < 8; ++i)
        (*out)[offset + i] = char((n >> (56 - 8 * i)) & 0xff);
}

quint64 getBe64(const uint8_t* p) {
    quint64 n = 0;
    for (int i = 0; i < 8; ++i)
        n = (n << 8) | p[i];
    return n;
}

quint32 getBe32(const uint8_t* p) {
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | p[3];
}

void fillNonce(uint8_t nonce[24], const char context[8], quint64 counter) {
    std::memcpy(nonce, context, 8);
    for (int i = 0; i < 8; ++i)
        nonce[8 + i] = uint8_t((counter >> (56 - 8 * i)) & 0xff);
    std::memset(nonce + 16, 0, 8);
}

void appendMessage(QByteArray* out, quint8 type, const QByteArray& body) {
    char head[4] = {0, char(type), char((body.size() >> 8) & 0xff), char(body.size() & 0xff)};
    out->append(head, 4);
    out->append(body);
}

bool sealRecord(QByteArray* wire, const uint8_t key[32], quint64 counter, const char context[8],
                const QByteArray& plain, int maxPlain) {
    if (plain.size() > maxPlain || plain.size() < 0)
        return false;
    QByteArray header(kHeader, 0);
    header[0] = 2;
    putBe64(&header, 1, counter);
    const auto n = quint32(plain.size());
    header[9] = char((n >> 24) & 0xff);
    header[10] = char((n >> 16) & 0xff);
    header[11] = char((n >> 8) & 0xff);
    header[12] = char(n & 0xff);
    uint8_t nonce[24];
    fillNonce(nonce, context, counter);
    wire->resize(kHeader + plain.size() + kTag);
    crypto_aead_lock(reinterpret_cast<uint8_t*>(wire->data()) + kHeader,
                     reinterpret_cast<uint8_t*>(wire->data()) + kHeader + plain.size(), key, nonce,
                     reinterpret_cast<const uint8_t*>(header.constData()), header.size(),
                     reinterpret_cast<const uint8_t*>(plain.constData()), size_t(plain.size()));
    std::memcpy(wire->data(), header.constData(), kHeader);
    crypto_wipe(nonce, sizeof nonce);
    return true;
}

bool openRecord(const uint8_t key[32], quint64 expected, const char context[8], const QByteArray& header,
                const uint8_t* cipher, int cipherLen, const uint8_t mac[16], QByteArray* plain) {
    const auto counter = getBe64(reinterpret_cast<const uint8_t*>(header.constData()) + 1);
    const auto length = getBe32(reinterpret_cast<const uint8_t*>(header.constData()) + 9);
    if (header.size() != kHeader || header[0] != 2 || counter != expected || int(length) != cipherLen)
        return false;
    uint8_t nonce[24];
    fillNonce(nonce, context, counter);
    plain->resize(cipherLen);
    const int bad = crypto_aead_unlock(reinterpret_cast<uint8_t*>(plain->data()), mac, key, nonce,
                                        reinterpret_cast<const uint8_t*>(header.constData()), header.size(), cipher,
                                        size_t(cipherLen));
    crypto_wipe(nonce, sizeof nonce);
    if (bad) {
        crypto_wipe(plain->data(), size_t(plain->size()));
        plain->clear();
        return false;
    }
    return true;
}

void random32(uint8_t out[32]) {
    for (int i = 0; i < 32; i += 4) {
        const quint32 r = QRandomGenerator::system()->generate();
        std::memcpy(out + i, &r, 4);
    }
}
}

FurinaChannel::FurinaChannel() = default;

FurinaChannel::~FurinaChannel() {
    wipe();
}

void FurinaChannel::wipe() {
    delete m_spake;
    m_spake = nullptr;
    crypto_wipe(m_sendKey, sizeof m_sendKey);
    crypto_wipe(m_recvKey, sizeof m_recvKey);
    crypto_wipe(m_ltkKey, sizeof m_ltkKey);
    crypto_wipe(m_cursor, sizeof m_cursor);
    crypto_wipe(m_ltk, sizeof m_ltk);
    if (!m_in.isEmpty()) {
        m_in.detach();
        crypto_wipe(m_in.data(), size_t(m_in.size()));
    }
    m_in.clear();
    m_frames.clear();
    m_sendCounter = m_recvCounter = 0;
    m_started = m_haveBanner = m_haveShare = m_haveConfirm = m_ready = m_presetKey = false;
}

void FurinaChannel::fail(const QString& reason) {
    wipe();
    m_error = reason;
}

void FurinaChannel::reset(Role role, const QByteArray& password, const QByteArray& existingLongTermKey) {
    wipe();
    m_error.clear();
    m_role = role;
    m_presetKey = existingLongTermKey.size() == 32;
    const QByteArray& secret = m_presetKey ? existingLongTermKey : password;
    if (m_presetKey)
        std::memcpy(m_ltk, existingLongTermKey.constData(), 32);
    m_spake = new Spake2Party(role == Role::Client, secret);
    if (!m_spake->valid())
        fail("握手无法开始。\nHandshake could not start.");
}

QByteArray FurinaChannel::start() {
    if (!m_spake || !m_spake->valid() || m_started)
        return {};
    QByteArray out("FUR2");
    out.append('\0');
    out.append('\x02');
    appendMessage(&out, 1, m_spake->share());
    m_started = true;
    return out;
}

bool FurinaChannel::deriveKeys() {
    const auto session = m_spake ? m_spake->sessionHash() : QByteArray();
    if (session.size() != 32)
        return false;
    const auto prk = furinaHkdfExtract(QByteArray("FUR2-session"), session);
    const auto c2s = furinaHkdfExpand(prk, "main-c2s", 32);
    const auto s2c = furinaHkdfExpand(prk, "main-s2c", 32);
    const auto cursor = furinaHkdfExpand(prk, "cursor-base", 32);
    const auto ltk = furinaHkdfExpand(prk, "ltk-wrap", 32);
    if (c2s.size() != 32 || s2c.size() != 32 || cursor.size() != 32 || ltk.size() != 32)
        return false;
    const QByteArray& send = m_role == Role::Client ? c2s : s2c;
    const QByteArray& recv = m_role == Role::Client ? s2c : c2s;
    std::memcpy(m_sendKey, send.constData(), 32);
    std::memcpy(m_recvKey, recv.constData(), 32);
    std::memcpy(m_cursor, cursor.constData(), 32);
    std::memcpy(m_ltkKey, ltk.constData(), 32);
    m_sendCounter = m_recvCounter = 0;
    return true;
}

QByteArray FurinaChannel::wrapLtk(bool ack) const {
    uint8_t nonce[24] = {};
    std::memcpy(nonce, ack ? "FUR2-ACK" : "FUR2-LTK", 8);
    const QByteArray plain = ack ? QByteArray("OKAY") : QByteArray(reinterpret_cast<const char*>(m_ltk), 32);
    QByteArray body(kTag + plain.size(), 0);
    crypto_aead_lock(reinterpret_cast<uint8_t*>(body.data()) + kTag, reinterpret_cast<uint8_t*>(body.data()), m_ltkKey,
                     nonce, reinterpret_cast<const uint8_t*>(ack ? "ACK" : "LTK"), 3,
                     reinterpret_cast<const uint8_t*>(plain.constData()), size_t(plain.size()));
    return body;
}

bool FurinaChannel::unwrapLtk(const QByteArray& body, bool ack, QByteArray* plain) const {
    const int expect = ack ? 4 : 32;
    if (body.size() != kTag + expect)
        return false;
    uint8_t nonce[24] = {};
    std::memcpy(nonce, ack ? "FUR2-ACK" : "FUR2-LTK", 8);
    plain->resize(expect);
    return crypto_aead_unlock(reinterpret_cast<uint8_t*>(plain->data()),
                               reinterpret_cast<const uint8_t*>(body.constData()), m_ltkKey, nonce,
                               reinterpret_cast<const uint8_t*>(ack ? "ACK" : "LTK"), 3,
                               reinterpret_cast<const uint8_t*>(body.constData()) + kTag, size_t(expect)) == 0;
}

bool FurinaChannel::onMessage(quint8 type, const QByteArray& body, QByteArray* egress) {
    if (type == 1) {
        if (m_haveShare || body.size() != 32 || !m_spake || !m_spake->finish(body)) {
            fail("密钥交换失败，已拒绝。\nKey exchange failed. Connection rejected.");
            return false;
        }
        m_haveShare = true;
        if (!deriveKeys()) {
            fail("会话密钥派生失败，已拒绝。\nSession key derivation failed. Connection rejected.");
            return false;
        }
        appendMessage(egress, 2, m_spake->confirmation());
        return true;
    }
    if (type == 2) {
        if (!m_haveShare || m_haveConfirm || body.size() != 32) {
            fail("握手确认无效，已拒绝。\nHandshake confirmation was rejected.");
            return false;
        }
        const auto expect = m_spake->peerConfirmation();
        if (crypto_verify32(reinterpret_cast<const uint8_t*>(body.constData()),
                            reinterpret_cast<const uint8_t*>(expect.constData())) != 0) {
            fail("配对失败，已拒绝。\nPairing failed. Connection rejected.");
            return false;
        }
        m_haveConfirm = true;
        if (m_role == Role::Server) {
            if (!m_presetKey)
                random32(m_ltk);
            appendMessage(egress, 3, wrapLtk(false));
        }
        return true;
    }
    if (type == 3) {
        if (m_role != Role::Client || !m_haveConfirm || m_ready) {
            fail("长期密钥消息无效，已拒绝。\nLong-term key message was rejected.");
            return false;
        }
        QByteArray plain;
        if (!unwrapLtk(body, false, &plain)) {
            fail("长期密钥校验失败，已拒绝。\nLong-term key check failed. Connection rejected.");
            return false;
        }
        if (m_presetKey &&
            crypto_verify32(m_ltk, reinterpret_cast<const uint8_t*>(plain.constData())) != 0) {
            crypto_wipe(plain.data(), size_t(plain.size()));
            fail("长期密钥不匹配，已拒绝。\nLong-term key mismatch. Connection rejected.");
            return false;
        }
        std::memcpy(m_ltk, plain.constData(), 32);
        crypto_wipe(plain.data(), size_t(plain.size()));
        appendMessage(egress, 4, wrapLtk(true));
        m_ready = true;
        return true;
    }
    if (type == 4) {
        if (m_role != Role::Server || !m_haveConfirm || m_ready) {
            fail("长期密钥确认无效，已拒绝。\nLong-term key acknowledgement was rejected.");
            return false;
        }
        QByteArray plain;
        if (!unwrapLtk(body, true, &plain) || plain != "OKAY") {
            fail("长期密钥确认无效，已拒绝。\nLong-term key acknowledgement was rejected.");
            return false;
        }
        m_ready = true;
        return true;
    }
    fail("握手消息无效，已拒绝。\nInvalid handshake message. Connection rejected.");
    return false;
}

bool FurinaChannel::parseRecords() {
    const char* context = m_role == Role::Client ? "F2mainS2" : "F2mainC2";
    while (m_in.size() >= kHeader) {
        const auto* raw = reinterpret_cast<const uint8_t*>(m_in.constData());
        if (raw[0] != 2) {
            fail("加密记录版本无效，已拒绝。\nEncrypted record version was rejected.");
            return false;
        }
        const auto length = getBe32(raw + 9);
        const auto counter = getBe64(raw + 1);
        if (length > FR_MAX_PAYLOAD + FR_HEADER_SIZE || counter != m_recvCounter) {
            fail("加密记录被拒绝（重放、乱序或长度无效）。\nEncrypted record rejected (replay, reorder, or invalid length).");
            return false;
        }
        const int total = kHeader + int(length) + kTag;
        if (m_in.size() < total)
            break;
        QByteArray plain;
        if (!openRecord(m_recvKey, m_recvCounter, context, m_in.left(kHeader), raw + kHeader, int(length), raw + kHeader + length,
                        &plain)) {
            fail("加密记录校验失败，已拒绝。\nEncrypted record authentication failed. Connection rejected.");
            return false;
        }
        m_in.remove(0, total);
        ++m_recvCounter;
        if (plain.size() < FR_HEADER_SIZE) {
            fail("内部帧无效，已拒绝。\nInner frame was rejected.");
            return false;
        }
        fr_header header{};
        if (!fr_decode_header(reinterpret_cast<const uint8_t*>(plain.constData()), &header) ||
            plain.size() != int(FR_HEADER_SIZE + header.length)) {
            fail("内部帧无效，已拒绝。\nInner frame was rejected.");
            return false;
        }
        Frame frame;
        frame.type = header.type;
        frame.payload = plain.mid(FR_HEADER_SIZE, header.length);
        m_frames.append(frame);
    }
    return true;
}

bool FurinaChannel::push(const QByteArray& chunk, QByteArray* egress) {
    if (!egress || !m_error.isEmpty() || !m_started)
        return false;
    m_in += chunk;
    if (m_in.size() > FR_MAX_PAYLOAD + FR_HEADER_SIZE + 64) {
        fail("接收缓存超出限制。\nReceive buffer exceeded.");
        return false;
    }
    if (!m_haveBanner) {
        if (m_in.size() < 4)
            return true;
        if (!m_in.startsWith("FUR2")) {
            fail(m_in.startsWith("FUR1")
                     ? QString("对端仍是 0.4.x 明文协议，已拒绝。\nPeer rejected: 0.4.x plaintext HMAC is not accepted (need FUR2).")
                     : QString("协议标识无效，已拒绝。\nUnrecognized protocol banner. Connection rejected."));
            return false;
        }
        if (m_in.size() < 6)
            return true;
        const uint16_t version = (uint16_t(uint8_t(m_in[4])) << 8) | uint8_t(m_in[5]);
        if (version != 2) {
            fail("不支持的 FUR2 版本，已拒绝。\nUnsupported FUR2 version. Connection rejected.");
            return false;
        }
        m_in.remove(0, 6);
        m_haveBanner = true;
    }
    while (!m_ready) {
        if (m_in.size() < 4)
            return true;
        const auto* raw = reinterpret_cast<const uint8_t*>(m_in.constData());
        const quint8 type = raw[1];
        const int length = (int(raw[2]) << 8) | raw[3];
        if (raw[0] != 0 || length > kMaxMessage) {
            fail("握手消息无效，已拒绝。\nInvalid handshake message. Connection rejected.");
            return false;
        }
        if (m_in.size() < 4 + length)
            return true;
        const auto body = m_in.mid(4, length);
        m_in.remove(0, 4 + length);
        if (!onMessage(type, body, egress))
            return false;
    }
    return parseRecords();
}

bool FurinaChannel::seal(quint8 type, const QByteArray& payload, QByteArray* wire) {
    if (!wire || !m_ready)
        return false;
    uint8_t header[FR_HEADER_SIZE];
    if (!fr_encode_header(header, type, quint32(payload.size())))
        return false;
    QByteArray plain(reinterpret_cast<const char*>(header), FR_HEADER_SIZE);
    plain += payload;
    const char* context = m_role == Role::Client ? "F2mainC2" : "F2mainS2";
    if (!sealRecord(wire, m_sendKey, m_sendCounter, context, plain, FR_MAX_PAYLOAD + FR_HEADER_SIZE))
        return false;
    ++m_sendCounter;
    return true;
}

bool FurinaChannel::pop(quint8* type, QByteArray* payload) {
    if (!type || !payload || m_frames.isEmpty())
        return false;
    const auto frame = m_frames.takeFirst();
    *type = frame.type;
    *payload = frame.payload;
    return true;
}

QByteArray FurinaChannel::cursorSecret() const {
    return m_ready ? QByteArray(reinterpret_cast<const char*>(m_cursor), 32) : QByteArray();
}

QByteArray FurinaChannel::longTermKey() const {
    return m_ready ? QByteArray(reinterpret_cast<const char*>(m_ltk), 32) : QByteArray();
}

CursorChannel::CursorChannel() = default;

CursorChannel::~CursorChannel() {
    wipe();
}

void CursorChannel::wipe() {
    crypto_wipe(m_sk, sizeof m_sk);
    crypto_wipe(m_pk, sizeof m_pk);
    crypto_wipe(m_peer, sizeof m_peer);
    crypto_wipe(m_sendKey, sizeof m_sendKey);
    crypto_wipe(m_recvKey, sizeof m_recvKey);
    crypto_wipe(m_expect, sizeof m_expect);
    if (!m_secret.isEmpty()) {
        m_secret.detach();
        crypto_wipe(m_secret.data(), size_t(m_secret.size()));
        m_secret.clear();
    }
    m_in.clear();
    m_packets.clear();
    m_sendCounter = m_recvCounter = 0;
    m_started = m_haveBanner = m_haveEph = m_ready = false;
}

void CursorChannel::fail(const QString& reason) {
    wipe();
    m_error = reason;
}

void CursorChannel::reset(bool client, const QByteArray& sessionSecret) {
    wipe();
    m_error.clear();
    m_client = client;
    if (sessionSecret.size() != 32) {
        fail("光标通道密钥无效。\nCursor channel key is invalid.");
        return;
    }
    m_secret = sessionSecret;
    random32(m_sk);
    crypto_x25519_public_key(m_pk, m_sk);
}

QByteArray CursorChannel::start() {
    if (m_secret.size() != 32 || m_started)
        return {};
    QByteArray out("FUR2");
    out.append('\0');
    out.append('\x02');
    appendMessage(&out, 1, QByteArray(reinterpret_cast<const char*>(m_pk), 32));
    m_started = true;
    return out;
}

bool CursorChannel::onMessage(quint8 type, const QByteArray& body, QByteArray* egress) {
    if (type == 1) {
        if (m_haveEph || body.size() != 32) {
            fail("光标握手无效，已拒绝。\nCursor handshake was rejected.");
            return false;
        }
        unsigned zeros = 0;
        for (char c : body)
            zeros |= uint8_t(c);
        if (!zeros) {
            fail("光标握手无效，已拒绝。\nCursor handshake was rejected.");
            return false;
        }
        std::memcpy(m_peer, body.constData(), 32);
        uint8_t dh[32];
        crypto_x25519(dh, m_sk, m_peer);
        crypto_wipe(m_sk, sizeof m_sk);
        unsigned empty = 0;
        for (unsigned char c : dh)
            empty |= c;
        if (!empty) {
            fail("光标握手无效，已拒绝。\nCursor handshake was rejected.");
            return false;
        }
        const QByteArray ephA = m_client ? QByteArray(reinterpret_cast<const char*>(m_pk), 32)
                                         : QByteArray(reinterpret_cast<const char*>(m_peer), 32);
        const QByteArray ephB = m_client ? QByteArray(reinterpret_cast<const char*>(m_peer), 32)
                                         : QByteArray(reinterpret_cast<const char*>(m_pk), 32);
        const auto prk = furinaHkdfExtract(m_secret, QByteArray(reinterpret_cast<const char*>(dh), 32) + ephA + ephB);
        crypto_wipe(dh, sizeof dh);
        const auto material = furinaHkdfExpand(prk, "FUR2-cursor-v2", 128);
        const auto confirmA = furinaHmacSha256(material.mid(0, 32), ephA + ephB);
        const auto confirmB = furinaHmacSha256(material.mid(32, 32), ephA + ephB);
        std::memcpy(m_sendKey, material.constData() + (m_client ? 64 : 96), 32);
        std::memcpy(m_recvKey, material.constData() + (m_client ? 96 : 64), 32);
        const auto self = m_client ? confirmA : confirmB;
        const auto peer = m_client ? confirmB : confirmA;
        std::memcpy(m_expect, peer.constData(), 32);
        m_haveEph = true;
        appendMessage(egress, 2, self);
        return true;
    }
    if (type == 2) {
        if (!m_haveEph || m_ready || body.size() != 32 ||
            crypto_verify32(reinterpret_cast<const uint8_t*>(body.constData()), m_expect) != 0) {
            fail("光标通道认证失败，已拒绝。\nCursor channel authentication failed. Connection rejected.");
            return false;
        }
        m_ready = true;
        m_sendCounter = m_recvCounter = 0;
        return true;
    }
    fail("光标握手无效，已拒绝。\nCursor handshake was rejected.");
    return false;
}

bool CursorChannel::parseRecords() {
    const char* context = m_client ? "F2cursS2" : "F2cursC2";
    constexpr int kMax = 1024 * 1024 + 16;
    while (m_in.size() >= kHeader) {
        const auto* raw = reinterpret_cast<const uint8_t*>(m_in.constData());
        if (raw[0] != 2) {
            fail("光标记录无效，已拒绝。\nCursor record was rejected.");
            return false;
        }
        const auto length = getBe32(raw + 9);
        const auto counter = getBe64(raw + 1);
        if (length < 4 || length > kMax || counter != m_recvCounter) {
            fail("光标记录被拒绝（重放、乱序或长度无效）。\nCursor record rejected (replay, reorder, or invalid length).");
            return false;
        }
        const int total = kHeader + int(length) + kTag;
        if (m_in.size() < total)
            break;
        QByteArray plain;
        if (!openRecord(m_recvKey, m_recvCounter, context, m_in.left(kHeader), raw + kHeader, int(length),
                        raw + kHeader + length, &plain)) {
            fail("光标记录校验失败，已拒绝。\nCursor record authentication failed. Connection rejected.");
            return false;
        }
        m_in.remove(0, total);
        ++m_recvCounter;
        m_packets.append(plain);
    }
    return true;
}

bool CursorChannel::push(const QByteArray& chunk, QByteArray* egress) {
    if (!egress || !m_error.isEmpty() || !m_started)
        return false;
    m_in += chunk;
    if (m_in.size() > 1024 * 1024 + 64) {
        fail("光标接收缓存超出限制。\nCursor receive buffer exceeded.");
        return false;
    }
    if (!m_haveBanner) {
        if (m_in.size() < 4)
            return true;
        if (!m_in.startsWith("FUR2")) {
            fail("光标协议标识无效，已拒绝。\nUnrecognized cursor protocol banner. Connection rejected.");
            return false;
        }
        if (m_in.size() < 6)
            return true;
        const uint16_t version = (uint16_t(uint8_t(m_in[4])) << 8) | uint8_t(m_in[5]);
        if (version != 2) {
            fail("不支持的 FUR2 版本，已拒绝。\nUnsupported FUR2 version. Connection rejected.");
            return false;
        }
        m_in.remove(0, 6);
        m_haveBanner = true;
    }
    while (!m_ready) {
        if (m_in.size() < 4)
            return true;
        const auto* raw = reinterpret_cast<const uint8_t*>(m_in.constData());
        const quint8 type = raw[1];
        const int length = (int(raw[2]) << 8) | raw[3];
        if (raw[0] != 0 || length > kMaxMessage) {
            fail("光标握手无效，已拒绝。\nCursor handshake was rejected.");
            return false;
        }
        if (m_in.size() < 4 + length)
            return true;
        const auto body = m_in.mid(4, length);
        m_in.remove(0, 4 + length);
        if (!onMessage(type, body, egress))
            return false;
    }
    return parseRecords();
}

bool CursorChannel::seal(const QByteArray& packet, QByteArray* wire) {
    if (!wire || !m_ready)
        return false;
    const char* context = m_client ? "F2cursC2" : "F2cursS2";
    if (!sealRecord(wire, m_sendKey, m_sendCounter, context, packet, 1024 * 1024 + 16))
        return false;
    ++m_sendCounter;
    return true;
}

bool CursorChannel::pop(QByteArray* packet) {
    if (!packet || m_packets.isEmpty())
        return false;
    *packet = m_packets.takeFirst();
    return true;
}

