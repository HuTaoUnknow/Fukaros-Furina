#pragma once
#include <QObject>
#include <QPushButton>
#include <QTimer>
#include "cast_ui_state.h"
#include "peer.h"

// One button owns start, pending cancellation and stopping an occupied session.
class CastControl : public QObject {
 Q_OBJECT
public:
 enum State { Start,Waiting,Stop };Q_ENUM(State)
 CastControl(QWidget *window,CastUiState *casting,Peer *peer);
 State state()const{return m_state;}
signals:
 void startRequested();void stopRequested();
private:
 QPushButton *m_button;CastUiState *m_casting;Peer *m_peer;QTimer m_animation;
 State m_state=Start;int m_angle=0;
 void refresh();void updateIcon();
};
