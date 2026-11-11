#include "secret_store.h"
#include <QJniEnvironment>
#include <QJniObject>

namespace {
jbyteArray toJava(QJniEnvironment& env, const QByteArray& bytes) {
    auto* array = env->NewByteArray(bytes.size());
    if (!array)
        return nullptr;
    env->SetByteArrayRegion(array, 0, bytes.size(), reinterpret_cast<const jbyte*>(bytes.constData()));
    return array;
}

QByteArray fromJava(QJniEnvironment& env, jbyteArray array) {
    if (!array)
        return {};
    const auto n = env->GetArrayLength(array);
    if (n <= 0)
        return {};
    QByteArray bytes(n, Qt::Uninitialized);
    env->GetByteArrayRegion(array, 0, n, reinterpret_cast<jbyte*>(bytes.data()));
    return bytes;
}
} // namespace

bool pairingPlatformStore(QSettings&, const QString& key, const QByteArray& plain) {
    QJniEnvironment env;
    auto array = toJava(env, plain);
    if (!array)
        return false;
    const auto ok = QJniObject::callStaticMethod<jboolean>(
        "com/furina/black/SecretBox", "store", "(Ljava/lang/String;[B)Z",
        QJniObject::fromString(key).object<jstring>(), array);
    env->DeleteLocalRef(array);
    return ok;
}

QByteArray pairingPlatformLoad(const QSettings&, const QString& key) {
    QJniEnvironment env;
    auto array = QJniObject::callStaticObjectMethod("com/furina/black/SecretBox", "load",
                                                    "(Ljava/lang/String;)[B",
                                                    QJniObject::fromString(key).object<jstring>());
    return fromJava(env, array.object<jbyteArray>());
}

bool pairingPlatformRemove(QSettings&, const QString& key) {
    return QJniObject::callStaticMethod<jboolean>("com/furina/black/SecretBox", "remove",
                                                  "(Ljava/lang/String;)Z",
                                                  QJniObject::fromString(key).object<jstring>());
}
