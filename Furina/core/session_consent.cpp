#include "session_consent.h"
#include <QRandomGenerator>

SessionConsent::SessionConsent(Peer *peer,QWidget *window):QObject(window),m_peer(peer),
 m_panel(window->findChild<QWidget*>("consentPanel")),m_label(window->findChild<QLabel*>("consentMessage")),
 m_accept(window->findChild<QPushButton*>("consentAccept")),m_reject(window->findChild<QPushButton*>("consentReject")){
 Q_ASSERT(m_panel&&m_label&&m_accept&&m_reject);m_panel->hide();
 m_timeout.setSingleShot(true);m_timeout.setInterval(30000);
 connect(m_accept,&QPushButton::clicked,this,[this]{answer(true);});
 connect(m_reject,&QPushButton::clicked,this,[this]{answer(false);});
 connect(peer,&Peer::approval,this,&SessionConsent::handle);
 connect(peer,&Peer::disconnected,this,&SessionConsent::clear);
 connect(&m_timeout,&QTimer::timeout,this,[this]{
  if(!m_waiting)return;
  m_peer->sendApproval(m_id,m_local?FR_APPROVAL_CANCEL:FR_APPROVAL_REJECT,m_options);clear();
  emit status("等待对端同意超时，投屏未启动。","Approval timed out. Casting did not start.");emit ended();
 });
}
void SessionConsent::clear(){m_timeout.stop();m_panel->hide();m_id=0;m_options=0;m_waiting=false;m_active=false;m_local=false;m_sharing=false;}
void SessionConsent::cancel(){if(m_id)m_peer->sendApproval(m_id,FR_APPROVAL_CANCEL,m_options);clear();}
bool SessionConsent::request(bool sharing,bool extended){
 if(!m_peer->ready()||m_waiting||m_requestsBlocked)return false;
 cancel();do{m_id=QRandomGenerator::system()->generate();}while(!m_id);
 m_options=sharing?FR_APPROVAL_SHARE:FR_APPROVAL_RECEIVE;
 if(extended)m_options|=FR_APPROVAL_EXTEND;
 if(extended&&!sharing&&m_receiverPortrait&&m_receiverPortrait())m_options|=FR_APPROVAL_PORTRAIT;
 m_sharing=sharing;m_local=true;m_waiting=true;m_timeout.start();
 emit status("开始等待对端同意。","Starting: waiting for the other device to approve.");
 m_peer->sendApproval(m_id,FR_APPROVAL_REQUEST,m_options);return true;
}
void SessionConsent::answer(bool accept){
 if(!m_waiting||m_local||!m_peer->ready())return;
 const auto id=m_id;auto options=m_options;
 if(!accept){clear();m_peer->sendApproval(id,FR_APPROVAL_REJECT,options);emit status("已拒绝投屏请求，投屏未启动。","Casting request rejected. Casting did not start.");emit ended();return;}
 m_waiting=false;m_active=true;m_timeout.stop();m_panel->hide();
 m_sharing=(options&FR_APPROVAL_RECEIVE)!=0;
 // The receiving device chooses the desktop orientation when approving.
 if(!m_sharing&&(options&FR_APPROVAL_EXTEND)&&m_receiverPortrait){
  options&=~FR_APPROVAL_PORTRAIT;if(m_receiverPortrait())options|=FR_APPROVAL_PORTRAIT;
 }
 m_options=options;
 emit status("已同意投屏请求，等待对端启动。","Casting request approved. Waiting for the requester to start.");
 emit approved(m_sharing,(options&FR_APPROVAL_EXTEND)!=0,false);
 m_peer->sendApproval(id,FR_APPROVAL_ACCEPT,options);
}
void SessionConsent::handle(quint32 id,quint32 verb,quint32 options){
 if(verb==FR_APPROVAL_REQUEST){
  if(m_requestsBlocked||m_waiting||m_active){m_peer->sendApproval(id,FR_APPROVAL_REJECT,options);return;}
  m_id=id;m_options=options;m_local=false;m_waiting=true;m_timeout.start();
  const bool peerSharing=(options&FR_APPROVAL_SHARE)!=0;
  const QString cn=peerSharing?"对端请求向本机分享屏幕。":"对端请求获取本机屏幕。";
  const QString en=peerSharing?"The other device wants to share its screen here.":"The other device wants to view this screen.";
  m_label->setText(cn+"\n"+en);emit incoming();m_panel->show();m_panel->raise();
  emit status(cn+"请选择同意或拒绝。",en+" Choose Allow or Reject.");return;
 }
 if(!m_id||id!=m_id)return;
 if(options!=m_options){
  // Only orientation may be selected by a receiver; direction/mode/ID remain fixed.
  const bool receiverChooses=m_local&&m_waiting&&m_sharing&&verb==FR_APPROVAL_ACCEPT&&(m_options&FR_APPROVAL_EXTEND);
  if(!receiverChooses||((options^m_options)&~FR_APPROVAL_PORTRAIT))return;
  m_options=options;
 }
 if(verb==FR_APPROVAL_CANCEL){clear();emit status("对端已取消投屏请求或停止投屏。","The other device cancelled the request or stopped casting.");emit ended();return;}
 if(!m_local||!m_waiting)return;
 if(verb==FR_APPROVAL_REJECT){clear();emit status("对端已拒绝投屏请求，投屏未启动。","The other device rejected the request. Casting did not start.");emit ended();return;}
 if(verb==FR_APPROVAL_ACCEPT){m_waiting=false;m_active=true;m_timeout.stop();emit status("对端已同意，正在启动投屏。","The other device approved. Starting casting.");emit approved(m_sharing,(options&FR_APPROVAL_EXTEND)!=0,true);}
}
