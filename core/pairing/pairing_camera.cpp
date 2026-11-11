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
#include <android/log.h>
#include <atomic>
#include <jni.h>

#define FURINA_LOG(...) __android_log_print(ANDROID_LOG_INFO, "Furina", __VA_ARGS__)

namespace {
QMutex g_callbackMutex;
PairingScanCallback g_callback;
std::atomic<int> g_decodeAttempts{0};

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

extern "C" JNIEXPORT jint JNICALL Java_com_furina_black_PairingCamera_nativeUprightDegrees(JNIEnv*, jclass,
                                                                                           jint sensor, jint display,
                                                                                           jboolean front) {
    return pairingCameraUprightDegrees(sensor, display, front != JNI_FALSE);
}

extern "C" JNIEXPORT jstring JNICALL Java_com_furina_black_PairingCamera_nativeDecode(
    JNIEnv* env, jclass, jbyteArray y, jint width, jint height, jint rowStride, jint pixelStride,
    jint uprightDegrees, jint displayDegrees) {
    const int attempt = ++g_decodeAttempts;
    if (!y || width < 21 || height < 21) {
        if (attempt == 1 || attempt % 30 == 0)
            FURINA_LOG("decode reject attempt=%d reason=size %dx%d", attempt, int(width), int(height));
        return nullptr;
    }
    const auto length = env->GetArrayLength(y);
    QByteArray bytes(length, Qt::Uninitialized);
    env->GetByteArrayRegion(y, 0, length, reinterpret_cast<jbyte*>(bytes.data()));
    const QImage luma = pairingExtractLuma(reinterpret_cast<const uchar*>(bytes.constData()), length, width, height,
                                            rowStride, pixelStride);
    if (luma.isNull()) {
        if (attempt == 1 || attempt % 30 == 0)
            FURINA_LOG("decode reject attempt=%d reason=luma %dx%d rowStride=%d pixelStride=%d bytes=%d "
                       "display=%d upright=%d",
                       attempt, int(width), int(height), int(rowStride), int(pixelStride), int(length),
                       int(displayDegrees), int(uprightDegrees));
        return nullptr;
    }
    const QImage upright = pairingRotateClockwise(luma, uprightDegrees);
    const auto found = pairingReadQrImage(upright);
    if (found.isEmpty()) {
        if (attempt == 1 || attempt % 30 == 0)
            FURINA_LOG("decode miss attempt=%d frame=%dx%d rowStride=%d pixelStride=%d bytes=%d upright=%dx%d "
                       "rotation=%d display=%d",
                       attempt, int(width), int(height), int(rowStride), int(pixelStride), int(length),
                       upright.width(), upright.height(), int(uprightDegrees), int(displayDegrees));
        return nullptr;
    }
    FURINA_LOG("decode ok attempt=%d frame=%dx%d upright=%dx%d rotation=%d display=%d", attempt, int(width),
               int(height), upright.width(), upright.height(), int(uprightDegrees), int(displayDegrees));
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
    // requestPermission is an instance method. A missing application still finishes the scan.
    auto* app = QCoreApplication::instance();
    if (!app) {
        if (callback)
            callback({});
        return;
    }
    app->requestPermission(QCameraPermission{}, context, [callback](const QPermission& permission) {
        if (permission.status() != Qt::PermissionStatus::Granted) {
            FURINA_LOG("camera permission denied");
            if (callback)
                callback({});
            return;
        }
        g_decodeAttempts.store(0);
        FURINA_LOG("camera permission granted");
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
