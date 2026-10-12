#include "cast_control.h"
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>

namespace {
class CastIcon final : public QIconEngine {
public:
 CastIcon(CastControl::State state,int angle):m_state(state),m_angle(angle){}
 QIconEngine *clone()const override{return new CastIcon(m_state,m_angle);}
 QPixmap pixmap(const QSize &size,QIcon::Mode mode,QIcon::State state)override{
  QPixmap result(size);result.fill(Qt::transparent);QPainter painter(&result);paint(&painter,QRect(QPoint(),size),mode,state);return result;
 }
 void paint(QPainter *p,const QRect &rect,QIcon::Mode mode,QIcon::State)override{
  p->save();p->setRenderHint(QPainter::Antialiasing);const qreal scale=qMin(rect.width(),rect.height())/24.0;
  p->translate(rect.center().x()+0.5-12*scale,rect.center().y()+0.5-12*scale);p->scale(scale,scale);
  const QColor color=mode==QIcon::Disabled?QColor("#768393"):QColor("#e4eaf0");
  if(m_state==CastControl::Start){
   QPainterPath triangle;triangle.moveTo(8,5);triangle.lineTo(19,12);triangle.lineTo(8,19);triangle.closeSubpath();p->setPen(Qt::NoPen);p->setBrush(color);p->drawPath(triangle);
  }else if(m_state==CastControl::Stop){
   p->setPen(QPen(color,3.0,Qt::SolidLine,Qt::RoundCap));p->drawLine(QPointF(8,5),QPointF(8,19));p->drawLine(QPointF(16,5),QPointF(16,19));
  }else{
   p->setPen(QPen(color,2.0,Qt::SolidLine,Qt::RoundCap));p->setBrush(Qt::NoBrush);p->translate(12,12);p->rotate(m_angle);p->drawArc(QRectF(-8,-8,16,16),30*16,280*16);
  }
  p->restore();
 }
private:CastControl::State m_state;int m_angle;
};
}
CastControl::CastControl(QWidget *window,CastUiState *casting,Peer *peer):QObject(window),m_button(window->findChild<QPushButton*>("start")),m_casting(casting),m_peer(peer){
 m_button->setText({});m_button->setIconSize(QSize(22,22));m_animation.setInterval(50);
 connect(&m_animation,&QTimer::timeout,this,[this]{m_angle=(m_angle+18)%360;updateIcon();});
 connect(m_casting,&CastUiState::changed,this,&CastControl::refresh);
 connect(m_peer,&Peer::connected,this,&CastControl::refresh);connect(m_peer,&Peer::disconnected,this,&CastControl::refresh);
 connect(m_button,&QPushButton::clicked,this,[this]{if(m_state==Start)emit startRequested();else emit stopRequested();});
 refresh();
}
void CastControl::refresh(){
 // A paired transport may remain connected after cancellation or rejection.
 // Only an occupied casting session shows Stop; pending consent shows Waiting.
 m_state=m_casting->occupied()?Stop:m_casting->locked()?Waiting:Start;
 const QString label=m_state==Start?QStringLiteral("开始"):m_state==Waiting?QStringLiteral("等待（点击取消）"):QStringLiteral("停止");
 m_button->setToolTip(label);m_button->setAccessibleName(label);m_button->setProperty("castState",m_state==Start?"start":m_state==Waiting?"waiting":"stop");
 if(m_state==Waiting){if(!m_animation.isActive()){m_angle=0;m_animation.start();}}else m_animation.stop();
 updateIcon();
}
void CastControl::updateIcon(){m_button->setIcon(QIcon(new CastIcon(m_state,m_angle)));}
