#pragma once
#include <QObject>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include "peer.h"

class PointerSettings : public QObject {
    Q_OBJECT
  public:
    PointerSettings(Peer* peer, QWidget* window, bool desktop);

    bool independentCursor() const {
        return m_independent;
    }

    double multiplier() const {
        return m_percent / 100.0;
    }

  signals:
    void changed(double multiplier);
    void independentCursorChanged(bool enabled);

  private:
    QCheckBox* m_mapping = nullptr;
    bool m_independent = true;
    quint64 m_mappingRevision = 0;
    void persistMapping();
    void sendMapping();
    void receiveMapping(bool enabled, quint64 revision);
    Peer* m_peer;
    QDoubleSpinBox* m_input;
    bool m_desktop;
    int m_percent = 100;
    quint64 m_revision = 0;
    void persist();
    void send();
    void receive(int percent, quint64 revision);
};
