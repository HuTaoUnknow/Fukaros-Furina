#pragma once
#include <QObject>
#include <QTimer>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include "peer.h"

// Authentication establishes the connection; this separately authorizes one
// direction for one casting session. IDs reject late replies after cancellation.
class SessionConsent : public QObject {
 Q_OBJECT
public:
 SessionConsent(Peer *peer,QWidget *window);
 bool request(bool sharing,bool extended);
 void cancel();
 // Keep new requests blocked after an occupied connection is lost until Stop.
 void setRequestsBlocked(bool blocked){m_requestsBlocked=blocked;}
 bool pending()const{return m_waiting;}
 bool active()const{return m_active;}
 bool sharing()const{return m_active&&m_sharing;}
 void setTimeout(int milliseconds){m_timeout.setInterval(milliseconds);}
signals:
 void status(const QString &chinese,const QString &english);
 void incoming();
 void approved(bool sharing,bool extended,bool initiatedHere);
 void ended();
private:
 Peer *m_peer;QWidget *m_panel;QLabel *m_label;QPushButton *m_accept,*m_reject;
 QTimer m_timeout;quint32 m_id=0,m_options=0;
 bool m_waiting=false,m_active=false,m_local=false,m_sharing=false,m_requestsBlocked=false;
 void clear();void answer(bool accept);void handle(quint32 id,quint32 verb,quint32 options);
};
