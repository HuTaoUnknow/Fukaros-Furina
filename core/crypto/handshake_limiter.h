#pragma once
#include <QHash>
#include <QString>
#include <functional>

/* Failed-handshake lockout. Five failures lock for five minutes, then twice as long
   on each later streak, capped at sixteen times the base (80 minutes). */
class HandshakeLimiter {
  public:
    static constexpr int kMaxFailures = 5;
    static constexpr qint64 kBaseLockMs = 5LL * 60 * 1000;

    void setClock(std::function<qint64()> nowMs);
    bool allow(const QString& address) const;
    void fail(const QString& address);
    void succeed(const QString& address);
    int generation(const QString& address) const;
    qint64 lockedUntil(const QString& address) const;

  private:
    struct Bucket {
        int fails = 0;
        int generation = 0;
        qint64 until = 0;
    };

    qint64 now() const;
    void bump(Bucket* bucket, qint64 at) const;
    const Bucket* find(const QString& address) const;

    std::function<qint64()> m_clock;
    QHash<QString, Bucket> m_byAddress;
    Bucket m_global;
};
