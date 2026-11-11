#pragma once
#include <QImage>
#include <QString>
#include <QStringList>

QStringList pairingLocalIpv4();
QImage pairingQrImage(const QString& text, int scale = 6);
QStringList pairingReadQrImage(const QImage& image);
