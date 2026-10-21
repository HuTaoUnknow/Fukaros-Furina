#pragma once
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QPoint>
#include "virtual_display.h"

class VirtualDisplayController : public QObject {
    Q_OBJECT
  public:
    explicit VirtualDisplayController(QObject* parent = nullptr);
    ~VirtualDisplayController() override;
    bool available() const;

    bool preparing() const {
        return m_preparing;
    }

    QString screenName() const;
    QString deviceName() const;
    bool position(QPoint* position) const;
    bool setPosition(const QPoint& position);
    void ensure(bool portrait = false);
    void setScalePercent(int percent);
    void release();
  signals:
    void ready(const QString& screenName);
    void scaleApplied(int percent);
    void scaleChanged();
    void status(const QString& chinese, const QString& english);
    void failed(const QString& chinese, const QString& english);

  private:
    fr_virtual_display m_display{};
    QTimer m_poll, m_releaseRetry;
    QElapsedTimer m_elapsed;
    unsigned m_releaseAttempts = 0;
    bool m_preparing = false, m_installing = false, m_portrait = false;
    int m_scalePercent = 150;
    bool m_scalePrepared = false;
    QElapsedTimer m_scaleSettling;
    void applyScale();
    QString m_qtName;
    void* m_installer = nullptr;
    void acquire();
    void poll();
    void fail(const QString& cn, const QString& en);
    void finishRelease();
};
