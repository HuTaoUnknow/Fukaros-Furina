#include <QApplication>
#include <QTimer>
#include <QSettings>
#include <cstdio>
#include "peer.h"
#include "desktop_video.h"
#include "virtual_display.h"

// Explicit real-device test. Tablet consent is required; changes only our VDD.
int main(int argc,char **argv){
 QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
 QSettings settings("Furina","Fukaros");const QString host=settings.value("host").toString(),token=settings.value("token").toString();
 if(host.isEmpty()||token.isEmpty())return 77;
 Peer peer;DesktopVideoSender sender;fr_virtual_display display{};int frames=0,feedbacks=0;bool approved=false,finished=false;const bool fullscreenTest=app.arguments().contains("--fullscreen-sync");int fullscreenPhase=0;
 auto finish=[&](int code){if(finished)return;finished=true;sender.stop();auto result=fr_virtual_release(&display);printf("RESULT exit=%d frames=%d feedback=%d detached=%ld\n",code,frames,feedbacks,result);fflush(stdout);peer.close(true);QTimer::singleShot(200,&app,[&,code,result]{app.exit(result?4:code);});};
 QObject::connect(&peer,&Peer::connected,&app,[&]{peer.sendApproval(9101,FR_APPROVAL_REQUEST,FR_APPROVAL_SHARE|FR_APPROVAL_EXTEND);puts("REQUESTED");fflush(stdout);});
 QObject::connect(&peer,&Peer::approval,&app,[&](quint32 id,quint32 verb,quint32 options){
  if(id!=9101)return;
  if(verb==FR_APPROVAL_ACCEPT){approved=true;const long result=fr_virtual_acquire_oriented(&display,(options&FR_APPROVAL_PORTRAIT)!=0);if(result){finish(3);return;}
   char name[128]{};fr_virtual_name(&display,name,sizeof(name));peer.sendProfile(1920);peer.sendSession(FR_START_RECEIVE);peer.sendSession(FR_EXTEND);sender.setResolution(1920);sender.start(QString::fromUtf8(name),{});puts("APPROVED");fflush(stdout);
   if(fullscreenTest)QTimer::singleShot(1500,&app,[&]{fullscreenPhase=1;peer.sendSession(FR_MAXIMIZE);});
  }else if(verb==FR_APPROVAL_CANCEL)finish(approved&&frames>100&&feedbacks>2?0:2);else if(verb==FR_APPROVAL_REJECT)finish(2);
 });
 QObject::connect(&sender,&DesktopVideoSender::packet,&app,[&](const QByteArray &packet){sender.accepted(peer.sendVideo(packet));++frames;});
 QObject::connect(&sender,&DesktopVideoSender::status,&app,[](const QString &s){printf("%s\n",s.toUtf8().constData());fflush(stdout);});
 QObject::connect(&peer,&Peer::session,&app,[&](quint32 action){
  if(action==FR_REQUEST_KEY)sender.requestKeyframe();
  if(action==FR_FULLSCREEN_CONFIRMED||action==FR_RESTORED_CONFIRMED){printf("FULLSCREEN_CONFIRM code=%u fullscreen=%d phase=%d\n",action,action==FR_FULLSCREEN_CONFIRMED,fullscreenPhase);fflush(stdout);
   if(fullscreenTest&&fullscreenPhase==1&&action==FR_FULLSCREEN_CONFIRMED){fullscreenPhase=2;peer.sendSession(FR_RESTORE);}
   else if(fullscreenTest&&fullscreenPhase==2&&action==FR_RESTORED_CONFIRMED){fullscreenPhase=3;puts("REMOTE_FULLSCREEN_ROUNDTRIP_PASSED");fflush(stdout);}
  }
 });
 QObject::connect(&peer,&Peer::feedback,&app,[&](double fps,double ms,unsigned queue,double shown){++feedbacks;printf("FEEDBACK decoded=%.1f shown=%.1f work=%.1f queue=%u\n",fps,shown,ms,queue);fflush(stdout);});
 QObject::connect(&peer,&Peer::status,&app,[](const QString &s){printf("PEER %s\n",s.toUtf8().constData());fflush(stdout);});
 QObject::connect(&peer,&Peer::disconnected,&app,[&]{if(!finished)finish(2);});
 QTimer::singleShot(150000,&app,[&]{peer.sendApproval(9101,FR_APPROVAL_CANCEL,FR_APPROVAL_SHARE|FR_APPROVAL_EXTEND);finish(2);});
 peer.connectTo(host,27183,token);return app.exec();
}
