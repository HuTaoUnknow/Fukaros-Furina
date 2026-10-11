#include "androidbridge.h"
#include <QByteArray>
#include <QMetaObject>
#include <QMutex>
#include <QSysInfo>
#include "video_policy.h"
#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <jni.h>
#endif
static AndroidBridge *bridge=nullptr;
static QMutex bridgeMutex;
AndroidBridge::AndroidBridge(QObject *p):QObject(p){QMutexLocker lock(&bridgeMutex);bridge=this;}
AndroidBridge::~AndroidBridge(){{QMutexLocker lock(&bridgeMutex);bridge=nullptr;}wantFrame=false;stopCapture();}
QString AndroidBridge::deviceCode() const{
#ifdef Q_OS_ANDROID
 return QJniObject::callStaticObjectMethod("com/furina/black/FurinaActivity","deviceCode","()Ljava/lang/String;").toString();
#else
 return QSysInfo::machineHostName();
#endif
}
void AndroidBridge::startCapture(){
#ifdef Q_OS_ANDROID
 QJniObject::callStaticMethod<void>("com/furina/black/FurinaActivity","requestCapture","()V");
#else
 emit status("桌面模拟：使用当前主屏幕代替 Android 录屏");
#endif
}
void AndroidBridge::stopCapture(){wantFrame=false;
#ifdef Q_OS_ANDROID
 QJniObject::callStaticMethod<void>("com/furina/black/FurinaActivity","stopCapture","()V");
#endif
}
void AndroidBridge::accessibility(){
#ifdef Q_OS_ANDROID
 QJniObject::callStaticMethod<void>("com/furina/black/FurinaActivity","openAccessibility","()V");
#else
 emit status("无障碍触控仅在 Android 可用；桌面模拟只记录控制事件");
#endif
}
void AndroidBridge::gesture(fr_gesture g){
#ifdef Q_OS_ANDROID
 QJniObject::callStaticMethod<void>("com/furina/black/FurinaActivity","remoteGesture","(IIIII)V",jint(g.x1),jint(g.y1),jint(g.x2),jint(g.y2),jint(g.duration_ms));
#else
 emit status(QString("模拟手势 (%1,%2) → (%3,%4), %5ms").arg(g.x1).arg(g.y1).arg(g.x2).arg(g.y2).arg(g.duration_ms));
#endif
}
void AndroidBridge::key(quint32 k){
#ifdef Q_OS_ANDROID
 QJniObject::callStaticMethod<void>("com/furina/black/FurinaActivity","remoteKey","(I)V",jint(k));
#else
 emit status(QString("模拟按键 %1").arg(k));
#endif
}
void AndroidBridge::setCaptureResolution(int edge){
#ifdef Q_OS_ANDROID
 QJniObject::callStaticMethod<void>("com/furina/black/VideoCore","setCaptureEdge","(I)V",jint(edge));
#else
 Q_UNUSED(edge);
#endif
}
void AndroidBridge::requestKeyframe(){
#ifdef Q_OS_ANDROID
 QJniObject::callStaticMethod<void>("com/furina/black/VideoCore","requestKeyframe","()V");
#endif
}
void AndroidBridge::receiveVideo(const QByteArray &packet){
#ifdef Q_OS_ANDROID
 QJniEnvironment env;auto array=env->NewByteArray(packet.size());env->SetByteArrayRegion(array,0,packet.size(),reinterpret_cast<const jbyte*>(packet.constData()));QJniObject::callStaticMethod<void>("com/furina/black/VideoCore","submit","([B)V",array);env->DeleteLocalRef(array);
#else
 Q_UNUSED(packet);
#endif
}
void AndroidBridge::resetVideo(){
#ifdef Q_OS_ANDROID
 QJniObject::callStaticMethod<void>("com/furina/black/VideoCore","reset","()V");
#endif
}
void AndroidBridge::updateVideoView(const QRect &r,bool visible,bool fullscreen){
#ifdef Q_OS_ANDROID
 QJniObject::callStaticMethod<void>("com/furina/black/VideoCore","show","(IIIIZZ)V",jint(r.x()),jint(r.y()),jint(r.width()),jint(r.height()),jboolean(visible),jboolean(fullscreen));
#else
 Q_UNUSED(r);Q_UNUSED(visible);Q_UNUSED(fullscreen);
#endif
}
#ifdef Q_OS_ANDROID
extern "C" JNIEXPORT jboolean JNICALL Java_com_furina_black_FurinaActivity_nativeVideo(JNIEnv *env,jclass,jbyteArray array){
 QMutexLocker lock(&bridgeMutex);if(!bridge||!array||!bridge->wantFrame)return false;
 if(bridge->videoPending.fetch_add(1)>=4){--bridge->videoPending;return false;}
 const auto n=env->GetArrayLength(array);if(n<=FR_VIDEO_HEADER||n>FR_MAX_PAYLOAD){--bridge->videoPending;return false;}
 QByteArray data(n,Qt::Uninitialized);env->GetByteArrayRegion(array,0,n,reinterpret_cast<jbyte*>(data.data()));fr_video_info info{};if(!fr_video_read(reinterpret_cast<const uint8_t*>(data.constData()),n,&info)){--bridge->videoPending;return false;}
 auto *b=bridge;QMetaObject::invokeMethod(b,[b,data]{emit b->video(data);--b->videoPending;},Qt::QueuedConnection);return true;
}
extern "C" JNIEXPORT void JNICALL Java_com_furina_black_FurinaActivity_nativeVideoStats(JNIEnv*,jclass,jboolean sender,jdouble fps,jdouble work,jint queue,jdouble displayed,jint w,jint h,jlong elapsed){
 QMutexLocker lock(&bridgeMutex);if(!bridge)return;auto *b=bridge;QMetaObject::invokeMethod(b,[b,sender,fps,work,queue,displayed,w,h,elapsed]{emit b->videoStats(sender,fps,work,unsigned(qMax(0,queue)),displayed,w,h,unsigned(qBound(jlong(0),elapsed,jlong(60000))));},Qt::QueuedConnection);
}
extern "C" JNIEXPORT void JNICALL Java_com_furina_black_FurinaActivity_nativeNeedKey(JNIEnv*,jclass){QMutexLocker lock(&bridgeMutex);if(!bridge)return;auto *b=bridge;QMetaObject::invokeMethod(b,[b]{emit b->needKey();},Qt::QueuedConnection);}
extern "C" JNIEXPORT void JNICALL Java_com_furina_black_FurinaActivity_nativeExitFullscreen(JNIEnv*,jclass){QMutexLocker lock(&bridgeMutex);if(!bridge)return;auto *b=bridge;QMetaObject::invokeMethod(b,[b]{emit b->exitFullscreen();},Qt::QueuedConnection);}
extern "C" JNIEXPORT jboolean JNICALL Java_com_furina_black_FurinaActivity_nativeWantFrame(JNIEnv*,jclass){QMutexLocker lock(&bridgeMutex);return bridge&&bridge->wantFrame.load()&&!bridge->pending.load();}
extern "C" JNIEXPORT void JNICALL Java_com_furina_black_FurinaActivity_nativeFrame(JNIEnv *env,jclass,jbyteArray array){
 QMutexLocker lock(&bridgeMutex);
 if(!bridge||!array||bridge->pending.exchange(true))return;
 const auto n=env->GetArrayLength(array);if(n<=0||n>FR_MAX_PAYLOAD){bridge->pending=false;return;}
 QByteArray data(n,Qt::Uninitialized);env->GetByteArrayRegion(array,0,n,reinterpret_cast<jbyte*>(data.data()));
 auto *b=bridge;QMetaObject::invokeMethod(b,[b,data]{b->pending=false;emit b->jpeg(data);},Qt::QueuedConnection);
}
extern "C" JNIEXPORT void JNICALL Java_com_furina_black_FurinaActivity_nativeStatus(JNIEnv *env,jclass,jstring message){
 QMutexLocker lock(&bridgeMutex);
 if(!bridge||!message)return;const char *s=env->GetStringUTFChars(message,nullptr);QString text=QString::fromUtf8(s);env->ReleaseStringUTFChars(message,s);
 auto *b=bridge;QMetaObject::invokeMethod(b,[b,text]{emit b->status(text);},Qt::QueuedConnection);
}
extern "C" JNIEXPORT void JNICALL Java_com_furina_black_FurinaActivity_nativeStopped(JNIEnv*,jclass){
 QMutexLocker lock(&bridgeMutex);
 if(!bridge)return;auto *b=bridge;b->wantFrame=false;QMetaObject::invokeMethod(b,[b]{emit b->captureStopped();},Qt::QueuedConnection);
}
#endif
