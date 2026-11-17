#pragma once
#include <QObject>
#include <QPushButton>
#include <QTimer>
#include <functional>
#include "cast_ui_state.h"
#include "peer.h"

// One button owns start, pending cancellation and stopping an occupied session.
// It is also disabled until the two devices share a long-term key: pairing is a
// one-off step in Settings, not something the home page does with the Start button.
class CastControl : public QObject {
    Q_OBJECT
  public:
    enum State { Start, Waiting, Stop };
    Q_ENUM(State);

    using HasKey = std::function<bool()>;
    // hasKey reports whether a shared long-term key exists. An empty callback assumes one
    // does, so widgets built without pairing still behave exactly as before.
    CastControl(QWidget* window, CastUiState* casting, Peer* peer, HasKey hasKey = {});

    State state() const {
        return m_state;
    }

    // True while the button is greyed out because no shared key exists yet.
    bool unpaired() const {
        return m_unpaired;
    }

    // Re-evaluate the key and casting state. Signals cover the live app, but a key
    // can also appear without a disconnect (tests inject one, future callers may too).
    void refresh();

  signals:
    void startRequested();
    void stopRequested();

  private:
    QPushButton* m_button = nullptr;
    CastUiState* m_casting = nullptr;
    Peer* m_peer = nullptr;
    HasKey m_hasKey;
    QTimer m_animation;
    State m_state = Start;
    bool m_unpaired = false;
    int m_angle = 0;
    void updateIcon();
};
