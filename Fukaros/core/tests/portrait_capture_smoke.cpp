#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QElapsedTimer>
#include <cstdio>
#include "desktop_video.h"
#include "virtual_display.h"
#include "video_policy.h"
class PortraitMarker:public QWidget{
 void paintEvent(QPaintEvent*)override{
  QPainter p(this);int w=width()/2,h=height()/2;p.fillRect(0,0,w,h,QColor(180,20,30));p.fillRect(w,0,width()-w,h,QColor(20,180,30));p.fillRect(0,h,w,height()-h,QColor(20,30,180));p.fillRect(w,h,width()-w,height()-h,QColor(180,170,20));
  p.setPen(Qt::white);p.setFont(QFont("Segoe UI",24));p.drawText(rect(),Qt::AlignCenter,"PORTRAIT\nTOP\n1200 x 1920");
 }
};
int main(int argc,char **argv){
 QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);const int screenCount=QGuiApplication::screens().size();
 fr_virtual_display display{};LONG result=fr_virtual_acquire_oriented(&display,1);if(result){printf("acquire=%ld\n",result);return 2;}
 char buffer[128]{};fr_virtual_name(&display,buffer,sizeof buffer);QString name=QString::fromUtf8(buffer);
 PortraitMarker marker;marker.setWindowFlags(Qt::FramelessWindowHint|Qt::Tool);DesktopVideoSender sender;DesktopVideoReceiver receiver;int good=0,total=0;bool badSize=false;int separateFrames=0;const bool independent=app.arguments().contains("--independent");sender.setSeparateCursor(independent);QElapsedTimer clock;clock.start();
 QObject::connect(&sender,&DesktopVideoSender::status,&app,[](const QString &s){printf("%s\n",s.toUtf8().constData());fflush(stdout);});
 QObject::connect(&sender,&DesktopVideoSender::packet,&app,[&](const QByteArray &packet){fr_video_info info{};if(fr_video_read((const uint8_t*)packet.constData(),packet.size(),&info)){if(info.width>=info.height)badSize=true;if(info.flags&FR_VIDEO_CURSOR_SEPARATE)++separateFrames;receiver.submit(packet);}sender.accepted(true);});
 QObject::connect(&receiver,&DesktopVideoReceiver::recover,&sender,&DesktopVideoSender::requestKeyframe);
 QObject::connect(&receiver,&DesktopVideoReceiver::frameReady,&app,[&]{QImage image;if(!receiver.takeImage(&image))return;++total;
  const QColor want[]={QColor(180,20,30),QColor(20,180,30),QColor(20,30,180),QColor(180,170,20)};bool valid=image.width()<image.height();
  for(int n=0;n<4;++n){QColor c=image.pixelColor(image.width()*(n%2?3:1)/4,image.height()*(n/2?3:1)/4);valid&=abs(c.red()-want[n].red())+abs(c.green()-want[n].green())+abs(c.blue()-want[n].blue())<55;}
  if(valid)++good;if(total==60){image.save("portrait-capture-decoded.png");printf("decoded=%dx%d correct=%d/%d\n",image.width(),image.height(),good,total);fflush(stdout);}
 });
 QTimer::singleShot(900,&app,[&]{
  QScreen *target=nullptr;for(auto *s:QGuiApplication::screens())if(s->name()==name||s->name()==QString::fromUtf8(display.monitor_name)||s->name().contains("VDD")){target=s;break;}
  if(!target){printf("screen-not-found\n");return;}marker.setGeometry(target->geometry());marker.show();marker.raise();sender.setResolution(1920);sender.start(name,target->geometry());
 });
 QTimer::singleShot(6000,&app,[&]{sender.stop();receiver.reset();marker.close();const auto released=fr_virtual_release(&display);printf("portrait frames=%d correct=%d separate=%d released=%ld\n",total,good,separateFrames,released);fflush(stdout);QTimer::singleShot(700,&app,[&,released]{app.exit(good>60&&(!independent||separateFrames>60)&&!badSize&&!released&&QGuiApplication::screens().size()==screenCount?0:1);});});
 return app.exec();
}
