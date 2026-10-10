#pragma once
#include <QWidget>
#include <QImage>
#include <QElapsedTimer>
#include "furina_protocol.h"
class ScreenView : public QWidget {
 Q_OBJECT
public:
 explicit ScreenView(QWidget *parent=nullptr);
 void setImage(const QImage &image);
 bool hasImage() const{return !m_image.isNull();}
 void setInteractive(bool enabled){m_interactive=enabled;}
 int takeDisplayedFrames(){const auto count=m_displayed;m_displayed=0;return count;}
signals:
 void gesture(fr_gesture value);
 void leaveFullScreen();
protected:
 void paintEvent(QPaintEvent*) override;
 void mousePressEvent(QMouseEvent*) override;
 void mouseReleaseEvent(QMouseEvent*) override;
 void keyPressEvent(QKeyEvent*) override;
private:
 QImage m_image;bool m_interactive=false,m_pressed=false;uint32_t m_x=0,m_y=0;QElapsedTimer m_press;
 quint64 m_revision=0,m_painted=0;int m_displayed=0;
};
