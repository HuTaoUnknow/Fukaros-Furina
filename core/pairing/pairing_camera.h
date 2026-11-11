#pragma once
#include <QString>
#include <functional>

class QObject;

using PairingScanCallback = std::function<void(const QString& payload)>;

// Android opens the camera. Other builds do nothing; the desktop simulation pastes the payload.
void pairingStartCameraScan(QObject* context, PairingScanCallback callback);
