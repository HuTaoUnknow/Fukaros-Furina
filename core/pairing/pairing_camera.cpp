#include "pairing_camera.h"

#ifdef Q_OS_ANDROID
#include "pairing_qr.h"
#include <QByteArray>
#include <QCoreApplication>
#include <QImage>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPermissions>
#include <jni.h>

namespace {
QMutex g_callbackMutex;
PairingScanCallback g_callback;

void deliver(const QString& payload) {
    PairingScanCallback callback;
    {
        QMutexLocker lock(&g_callbackMutex);
        callback = std::move(g_callback);
        g_callback = nullptr;
    }
    if (callback)
        callback(payload);
}
} // namespace

extern "C" JNIEXPORT jstring JNICALL Java_com_furina_black_PairingCamera_nativeDecode(JNIEnv* env, jclass,
                                                                                     jbyteArray y, jint width,
                                                                                     jint height, jint stride) {
    if (!y || width < 21 || height < 21 || stride < width)
        return nullptr;
    const auto length = env->GetArrayLength(y);
    if (length < static_cast<jsize>(stride) * height)
        return nullptr;
    QByteArray bytes(length, Qt::Uninitialized);
    env->GetByteArrayRegion(y, 0, length, reinterpret_cast<jbyte*>(bytes.data()));
    QImage frame(reinterpret_cast<const uchar*>(bytes.constData()), width, height, stride,
                 QImage::Format_Grayscale8);
    const auto found = pairingReadQrImage(frame);
    if (found.isEmpty())
        return nullptr;
    const auto utf8 = found.first().toUtf8();
    return env->NewStringUTF(utf8.constData());
}

extern "C" JNIEXPORT void JNICALL Java_com_furina_black_PairingCamera_nativeFinished(JNIEnv* env, jclass,
                                                                                    jstring text) {
    QString payload;
    if (text) {
        const char* chars = env->GetStringUTFChars(text, nullptr);
        if (chars) {
            payload = QString::fromUtf8(chars);
            env->ReleaseStringUTFChars(text, chars);
        }
    }
    QMetaObject::invokeMethod(
        qApp, [payload] { deliver(payload); }, Qt::QueuedConnection);
}

void pairingStartCameraScan(QObject* context, PairingScanCallback callback) {
    QCoreApplication::requestPermission(QCameraPermission{}, context, [callback](const QPermission& permission) {
        if (permission.status() != Qt::PermissionStatus::Granted) {
            if (callback)
                callback({});
            return;
        }
        {
            QMutexLocker lock(&g_callbackMutex);
            g_callback = callback;
        }
        QJniObject::callStaticMethod<void>("com/furina/black/PairingCamera", "start", "()V");
    });
}
#else
void pairingStartCameraScan(QObject*, PairingScanCallback) {}
#endif
