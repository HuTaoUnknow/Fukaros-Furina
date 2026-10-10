#pragma once
#include <QObject>
#include <QThread>
#include <QImage>
#include <QRect>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
class DesktopVideoSender:public QObject {
 Q_OBJECT
public:
 explicit DesktopVideoSender(QObject *parent=nullptr);~DesktopVideoSender() override;
 void start(const QString &display,const QRect &geometry);void stop();
 void refreshDisplay(const QString &display);
 void setResolution(int edge);void feedback(double fps,unsigned queueMs,double remoteWorkMs=0);void setReceiverWork(double ms){m_localDecodeMs=ms;}
 void accepted(bool delivered);void requestKeyframe(){m_key=true;}
signals:
 void packet(const QByteArray &data);void status(const QString &message);
 void stats(double fps,double workMs,int edge);
 void failed();
private:
 QThread *m_thread=nullptr;std::atomic_bool m_stop{false},m_key{true},m_pending{false};
 std::atomic_bool m_refresh{false};std::atomic_uint m_epoch{0};
 std::mutex m_sourceMutex;QString m_display;quint64 m_runToken=0;
 std::atomic_int m_resolution{0};std::atomic<double> m_remoteFps{60},m_remoteWorkMs{0},m_localDecodeMs{0};std::atomic_uint m_queueMs{0};
 void run();
};
class DesktopVideoReceiver:public QObject {
 Q_OBJECT
public:
 explicit DesktopVideoReceiver(QObject *parent=nullptr);~DesktopVideoReceiver() override;
 void submit(const QByteArray &packet);void reset();
 /* GUI consumes the latest image; decoded frames do not queue UI events. */
 bool takeImage(QImage *image);
signals:
 void status(const QString &message);void recover();void frameReady();void stats(double fps,double workMs,unsigned queueMs);
private:
 QThread *m_thread=nullptr;std::mutex m_mutex;std::condition_variable m_wake;
 std::deque<QByteArray> m_packets;QImage m_latest;
 uint64_t m_generation=0;bool m_stop=false,m_reset=false,m_waitKey=true,m_notifyPending=false;void run();
};
