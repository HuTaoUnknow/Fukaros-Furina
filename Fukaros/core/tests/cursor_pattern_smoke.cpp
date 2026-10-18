#include <QApplication>
#include <QScreen>
#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
// Manual visual fixture only; never injects mouse movement or changes displays.
class Pattern:public QWidget{
public:
 Pattern(){setWindowTitle("Furina cursor verification");setMouseTracking(true);resize(600,800);}
 void paintEvent(QPaintEvent*)override{QPainter p(this);p.fillRect(rect(),Qt::white);for(int y=0;y<height();y+=100)for(int x=0;x<width();x+=100)if((x/100+y/100)%2)p.fillRect(x,y,100,100,QColor(30,45,65));p.setPen(QColor(0,180,240));p.setFont(QFont("Arial",22));p.drawText(QRect(10,20,width()-20,110),Qt::AlignCenter,"Independent cursor verification\n1200 x 1920 portrait");p.setPen(QPen(Qt::red,2));p.drawLine(width()/2,150,width()/2,height()-20);p.drawLine(10,height()/2,width()-10,height()/2);}
};
int main(int argc,char **argv){QApplication app(argc,argv);Pattern w;for(auto *screen:app.screens())if(screen!=app.primaryScreen()){w.move(screen->geometry().topLeft()+QPoint(40,80));break;}w.show();return app.exec();}
