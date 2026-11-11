#include "handshake_limiter.h"
#include <QDateTime>
#include <algorithm>

void HandshakeLimiter::setClock(std::function<qint64()> nowMs) {
    m_clock = std::move(nowMs);
}

qint64 HandshakeLimiter::now() const {
    return m_clock ? m_clock() : QDateTime::currentMSecsSinceEpoch();
}

void HandshakeLimiter::bump(Bucket* bucket, qint64 at) const {
    if (bucket->until > at)
        return;
    if (bucket->until != 0 && bucket->until <= at) {
        bucket->fails = 0;
        bucket->until = 0;
    }
    ++bucket->fails;
    if (bucket->fails < kMaxFailures)
        return;
    const int shift = std::min(bucket->generation, 4);
    bucket->until = at + (kBaseLockMs << shift);
    ++bucket->generation;
    bucket->fails = 0;
}

bool HandshakeLimiter::allow(const QString& address) const {
    const auto at = now();
    if (m_global.until > at)
        return false;
    const auto it = m_byAddress.constFind(address);
    return it == m_byAddress.cend() || it->until <= at;
}

void HandshakeLimiter::fail(const QString& address) {
    const auto at = now();
    if (!allow(address))
        return;
    bump(&m_global, at);
    bump(&m_byAddress[address], at);
}

void HandshakeLimiter::succeed(const QString& address) {
    m_byAddress.remove(address);
    m_global = {};
}

const HandshakeLimiter::Bucket* HandshakeLimiter::find(const QString& address) const {
    if (address.isEmpty())
        return &m_global;
    const auto it = m_byAddress.constFind(address);
    return it == m_byAddress.cend() ? nullptr : &(*it);
}

int HandshakeLimiter::generation(const QString& address) const {
    const auto* bucket = find(address);
    return bucket ? bucket->generation : 0;
}

qint64 HandshakeLimiter::lockedUntil(const QString& address) const {
    const auto* bucket = find(address);
    return bucket ? bucket->until : 0;
}
