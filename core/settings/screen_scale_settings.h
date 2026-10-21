#pragma once
#include <QObject>
#include <QComboBox>
#include <QCheckBox>
#include "peer.h"

class ScreenScaleSettings : public QObject {
    Q_OBJECT
  public:
    ScreenScaleSettings(Peer* peer, QWidget* window, bool desktop);

    int percent() const {
        return m_enabled ? m_percent : 100;
    }

    // An OS limit/failure can return the actual applied value without an apply loop.
    void confirmApplied(int percent);
  signals:
    void changed(int percent);

  private:
    Peer* m_peer;
    QComboBox* m_input;
    QCheckBox* m_toggle;
    bool m_enabled = true;
    bool m_desktop;
    int m_percent = 150;
    quint64 m_revision = 0;
    void persist();
    void send();
    void receive(int percent, quint64 revision, bool enabled);
    void updateControls();
};
