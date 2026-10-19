#pragma once
#include <QObject>
#include <QWidget>
#include <QCheckBox>
#include <QPushButton>
#include "screenview.h"
#include "session_consent.h"

// Freeze the negotiated casting options, including during the approval handshake.
// An occupied session stays locked after a transport failure until Stop resets it.
class CastUiState : public QObject {
    Q_OBJECT
  public:
    explicit CastUiState(QWidget* window);

    bool locked() const {
        return m_locked;
    }

    bool occupied() const {
        return m_occupied;
    }

    void lockPending();
    void occupy(bool sharing, bool extended);
    void reset();
    void connectionLost();
    void preserveSelection();
  signals:
    void changed();

  private:
    QCheckBox *m_share, *m_receive, *m_duplicate, *m_extend;
    ScreenView* m_view;
    QWidget* m_window;
    QPushButton* m_save;
    SessionConsent* m_consent;
    bool m_locked = false, m_occupied = false, m_sharing = false, m_extended = false, m_restoring = false;
    void updateControls();
};
