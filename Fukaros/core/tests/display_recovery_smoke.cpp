#include <QApplication>
#include <QScreen>
#include <QWidget>
#include <QPainter>
#include <QTimer>
#include <QElapsedTimer>
#include <vector>
#include <cstdio>
#include "desktop_video.h"
#include "virtual_display.h"
#include "furina_protocol.h"
#include "video_policy.h"
// Manual real-driver test: change only the acquired Furina source position.
static LONG moveSource(const wchar_t *device,int x,int y){
 UINT32 np=0,nm=0;LONG result=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&np,&nm);if(result)return result;
 std::vector<DISPLAYCONFIG_PATH_INFO> paths(np);std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
 result=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&np,paths.data(),&nm,modes.data(),nullptr);if(result)return result;
 bool found=false;
 for(UINT32 i=0;i<np;++i){DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};name.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;name.header.size=sizeof(name);name.header.adapterId=paths[i].sourceInfo.adapterId;name.header.id=paths[i].sourceInfo.id;
  if(!DisplayConfigGetDeviceInfo(&name.header)&&!wcscmp(name.viewGdiDeviceName,device)){unsigned index=paths[i].sourceInfo.modeInfoIdx;if(index>=nm||modes[index].infoType!=DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)return ERROR_INVALID_DATA;modes[index].sourceMode.position={x,y};found=true;}
 }
 if(!found)return ERROR_NOT_FOUND;
 result=SetDisplayConfig(np,paths.data(),nm,modes.data(),SDC_VALIDATE|SDC_USE_SUPPLIED_DISPLAY_CONFIG|SDC_ALLOW_CHANGES);
 if(!result)result=SetDisplayConfig(np,paths.data(),nm,modes.data(),SDC_APPLY|SDC_USE_SUPPLIED_DISPLAY_CONFIG|SDC_ALLOW_CHANGES|SDC_SAVE_TO_DATABASE);
 return result;
}
static QColor colors[]={{12,35,70},{140,45,20},{10,90,35},{85,25,130},{12,70,95},{150,80,20},{90,25,45}};
class Marker:public QWidget {
public:int stage=0,frame=0;
 void paintEvent(QPaintEvent*)override{QPainter p(this);p.fillRect(rect(),colors[stage]);p.setPen(Qt::white);p.setFont(QFont("Segoe UI",24));p.drawText(rect(),Qt::AlignCenter,QString("Capture recovery\nStage %1 | Frame %2").arg(stage).arg(frame));p.fillRect((frame*5)%width(),20,40,40,QColor(40,230,180));}
};
int main(int argc,char **argv){
 QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);const int originalCount=QGuiApplication::screens().size();const QString primary=QGuiApplication::primaryScreen()->name();
 fr_virtual_display display{};LONG result=fr_virtual_acquire(&display);if(result){fprintf(stderr,"acquire %ld\n",result);return 3;}
 char name[128]{};if(!fr_virtual_name(&display,name,sizeof name)){fr_virtual_release(&display);return 3;}
 const QString source=QString::fromUtf8(name);DesktopVideoSender sender;DesktopVideoReceiver receiver;Marker marker;int seen[7]={},samples[7]={};uint32_t lastEpoch=0;int epochs=0;bool valid=true,ended=false;QElapsedTimer age;age.start();
 auto place=[&]{for(auto *s:QGuiApplication::screens())if(s->name().contains("VDD")){marker.resize(850,520);marker.move(s->geometry().topLeft()+QPoint(30,30));marker.show();marker.raise();return;}};
 QObject::connect(&sender,&DesktopVideoSender::packet,&app,[&](const QByteArray &packet){fr_video_info info{};if(!fr_video_read((const uint8_t*)packet.constData(),packet.size(),&info)){valid=false;return;}if(info.epoch!=lastEpoch){if(lastEpoch&&info.epoch<=lastEpoch)valid=false;if(!(info.flags&FR_VIDEO_KEY))valid=false;lastEpoch=info.epoch;++epochs;}receiver.submit(packet);sender.accepted(true);});
 QObject::connect(&sender,&DesktopVideoSender::status,&app,[](const QString &s){fprintf(stdout,"%s\n",s.toUtf8().constData());fflush(stdout);});
 QObject::connect(&sender,&DesktopVideoSender::failed,&app,[&]{valid=false;});
 QObject::connect(&receiver,&DesktopVideoReceiver::recover,&sender,&DesktopVideoSender::requestKeyframe);
 QObject::connect(&receiver,&DesktopVideoReceiver::frameReady,&app,[&]{QImage image;if(ended||!receiver.takeImage(&image)||image.isNull())return;const QColor actual=image.pixelColor(qRound(image.width()*100.0/1920),qRound(image.height()*100.0/1200));const QColor want=colors[marker.stage];if((++samples[marker.stage])%60==0){printf("PIXEL stage=%d rgb=%d,%d,%d age=%lld\n",marker.stage,actual.red(),actual.green(),actual.blue(),(long long)age.elapsed());fflush(stdout);if(marker.stage==3)image.save("work/quality-probe/recovery-stage3.png");}if(abs(actual.red()-want.red())+abs(actual.green()-want.green())+abs(actual.blue()-want.blue())<24){if(!seen[marker.stage]){printf("LIVE stage=%d recovered_ms=%lld epoch=%u\n",marker.stage,(long long)age.elapsed(),lastEpoch);fflush(stdout);}++seen[marker.stage];}});
 QTimer tick;tick.setTimerType(Qt::PreciseTimer);tick.setInterval(16);QObject::connect(&tick,&QTimer::timeout,&app,[&]{++marker.frame;marker.update();});tick.start();
 QTimer::singleShot(500,&app,[&]{place();sender.setResolution(1080);sender.start(source,{});age.restart();});
 const POINT points[]={{-1920,0},{0,-1200},{0,1600},{2560,0}};
 for(int i=0;i<6;++i)QTimer::singleShot(3500+i*3000,&app,[&,i]{marker.stage=i+1;age.restart();if(i<4){LONG moved=moveSource(display.name,points[i].x,points[i].y);if(moved)valid=false;printf("MOVE stage=%d result=%ld\n",marker.stage,moved);QTimer::singleShot(500,&app,place);}else if(i==4)sender.refreshDisplay(source);else sender.start(source,{});marker.update();});
 QTimer::singleShot(21000,&app,[&]{ended=true;tick.stop();sender.stop();receiver.reset();marker.close();LONG released=fr_virtual_release(&display);if(released)valid=false;});
 QTimer::singleShot(22000,&app,[&]{valid=valid&&epochs>=7&&QGuiApplication::screens().size()==originalCount&&QGuiApplication::primaryScreen()->name()==primary;printf("RESULT epochs=%d colors=",epochs);for(int n:seen){printf("%d,",n);valid=valid&&n>20;}printf(" restored=%d passed=%d\n",QGuiApplication::screens().size()==originalCount,valid);app.exit(valid?0:2);});
 return app.exec();
}
