#pragma once
#include <QObject>
#include <atomic>
#include <QRect>
#include "furina_protocol.h"
class AndroidBridge:public QObject{
 Q_OBJECT
public:
 explicit AndroidBridge(QObject *parent=nullptr);
 ~AndroidBridge() override;
 void startCapture();void stopCapture();void accessibility();void usbSettings();void gesture(fr_gesture g);void key(quint32 k);
 QString deviceCode() const;
 void setExtensionOrientation(int direction);
 void setCaptureResolution(int edge);void requestKeyframe();void receiveVideo(const QByteArray &packet);void resetVideo();
 void receiveCursor(const QByteArray &packet);
 void keepSession(bool active);void backgroundTask();
 void updateVideoView(const QRect &physicalRect,bool visible,bool fullscreen);
 std::atomic_bool wantFrame{false},pending{false};
 std::atomic_int videoPending{0};
signals:
 void jpeg(const QByteArray &frame);
 void status(const QString &message);
 void captureStopped();
 void video(const QByteArray &packet);
 void videoStats(bool sender,double fps,double workMs,unsigned queueMs,double displayedFps,int width,int height,unsigned elapsedMs=1000);
 void needKey();void exitFullscreen();
 void backRequested();void stopRequested();
};
