#include "presentation_sync.h"
#include "peer.h"
#include "interface_support.h"
#include "session_consent.h"

PresentationSync::PresentationSync(Peer *peer,Presentation *presentation,SessionConsent *consent,QObject *parent):QObject(parent),m_peer(peer),m_presentation(presentation),m_consent(consent){
 m_timeout.setSingleShot(true);m_timeout.setInterval(2500);
 connect(&m_timeout,&QTimer::timeout,this,[this]{m_pending=false;emit status("尚未收到对端全屏状态确认，按钮保留已确认的状态。","Fullscreen confirmation has not arrived; the button retains the confirmed state.");});
 connect(peer,&Peer::session,this,&PresentationSync::session);
 connect(peer,&Peer::disconnected,this,&PresentationSync::reset);
 connect(consent,&SessionConsent::ended,this,&PresentationSync::reset);
 connect(consent,&SessionConsent::approved,this,[this]{
  reset();
  // A receiver must send APPROVAL_ACCEPT before its initial state report.
  QTimer::singleShot(0,this,&PresentationSync::confirmLocal);
 });
 connect(presentation,&Presentation::changed,this,[this]{confirmLocal();});
}
void PresentationSync::reset(){m_timeout.stop();m_pending=false;m_target=false;m_remote=false;emit remoteChanged(false);}
bool PresentationSync::toggleRemote(){
 if(!m_peer->ready()||!m_consent->sharing())return false;
 m_target=!(m_pending?m_target:m_remote);m_pending=true;m_timeout.start();
 m_peer->sendSession(m_target?FR_MAXIMIZE:FR_RESTORE);return m_target;
}
void PresentationSync::confirmLocal(){
 if(m_peer->ready()&&m_consent->active()&&!m_consent->sharing())m_peer->sendSession(m_presentation->active()?FR_FULLSCREEN_CONFIRMED:FR_RESTORED_CONFIRMED);
}
void PresentationSync::session(quint32 action){
 if(!m_peer->ready()||!m_consent->active())return;
 if(action==FR_MAXIMIZE||action==FR_RESTORE){
  if(m_consent->sharing())return;
  const bool wanted=action==FR_MAXIMIZE;
  if(m_presentation->active()==wanted)confirmLocal();else m_presentation->setActive(wanted);
 }else if(action==FR_FULLSCREEN_CONFIRMED||action==FR_RESTORED_CONFIRMED){
  if(!m_consent->sharing())return;
  m_remote=action==FR_FULLSCREEN_CONFIRMED;
  if(m_pending&&m_target==m_remote){m_pending=false;m_timeout.stop();}
  emit remoteChanged(m_remote);
 }
}
