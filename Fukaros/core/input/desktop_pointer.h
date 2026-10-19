#pragma once
#include <QObject>
#include <QTimer>
#include "pointer_gain.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
class DesktopPointer : public QObject {
    Q_OBJECT
  public:
    explicit DesktopPointer(QObject* parent = nullptr);
    ~DesktopPointer() override;
    void setMultiplier(double multiplier);
    void setDisplay(const QString& name);
    void stop();
  signals:
    void status(const QString& message);

  private:
    QString m_display;
    int m_percent = 100;
    QTimer m_refresh;
    void refresh();
#ifdef Q_OS_WIN
    HHOOK m_hook = nullptr;
    RECT m_bounds{};
    POINT m_last{};
    bool m_warping = false, m_valid = false;
    fr_pointer_fraction m_fraction{};
    static DesktopPointer* active;
    static LRESULT CALLBACK hook(int code, WPARAM message, LPARAM data);
#endif
};
