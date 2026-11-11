#include "mainwindow.h"
#include "cast_direction_settings.h"
#include "debug_log.h"
#include "pairing_code.h"
#include "pairing_identity.h"
#include "pairing_payload.h"
#include "pairing_qr.h"
#include "pairing_session.h"
#include "secret_store.h"
#include "ui_mainwindow.h"
#include "ui_settings.h"
#include "settings_support.h"
#include <QDateTime>
#include <QGuiApplication>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QFileInfo>
#include <QHostAddress>
#include <QSignalBlocker>
#include <QDesktopServices>
#include <QUrl>
#include <QResizeEvent>
#include <QButtonGroup>
#include "desktop_tray.h"

void MainWindow::loadSettings() {
    QSettings s;
    QString adb = s.value("adb", QStandardPaths::findExecutable("adb")).toString();
    if (adb.isEmpty()) {
        const QString bundled = QCoreApplication::applicationDirPath() + "/adb/adb.exe";
        if (QFileInfo::exists(bundled))
            adb = bundled;
    }
    cfg->adbPath->setText(adb);
    cfg->serial->setText(s.value("serial").toString());
    cfg->host->setText(s.value("host", QString()).toString());
    cfg->token->setText(QString::fromUtf8(
        pairingMigratePlaintext(SecretStore::instance(), s, QStringLiteral("token"), QStringLiteral("manual-code"))));
    refreshPairingHint();
    {
        const QSignalBlocker block(m_resolution);
        m_resolution->setValue(s.value("resolution", 0).toInt());
    }
    m_videoSender.setResolution(m_resolution->value());
    cfg->usbMode->setCurrentIndex(s.value("usbMode", 0).toInt() == 1 ? 1 : 0);
    cfg->usbHost->setText(s.value("usbHost").toString());
    cfg->transport->setCurrentIndex(s.value("transport", 0).toInt() == 1 ? 1 : 0);
    cfg->adbEndpoint->setText(s.value("adbEndpoint").toString());
}

void MainWindow::saveSettings() {
    furinaSaveCastDirection(m_direction->sharing());
    QSettings s;
    s.setValue("resolution", m_resolution->value());
    s.setValue("adb", cfg->adbPath->text());
    s.setValue("serial", cfg->serial->text());
    s.setValue("host", cfg->host->text());
    const auto code = pairingNormalizeManualCode(cfg->token->text());
    auto& secrets = SecretStore::instance();
    if (code.isEmpty())
        secrets.remove(QStringLiteral("manual-code"));
    else if (!secrets.store(QStringLiteral("manual-code"), code.toUtf8()))
        log("配对码未能写入安全存储。", "Could not store the pairing code.");
    s.remove(QStringLiteral("token"));
    s.setValue("usbMode", cfg->usbMode->currentIndex());
    s.setValue("usbHost", cfg->usbHost->text());
    s.setValue("transport", cfg->transport->currentIndex());
    s.setValue("adbEndpoint", cfg->adbEndpoint->text());
    s.setValue("castMode", ui->extend->isChecked() ? 1 : 0);
}

void MainWindow::openSettings() {
    const auto size = screen()->availableGeometry().size();
    m_settings->resize(qMin(560, size.width() - 24), qMin(690, size.height() - 24));
    m_settings->open();
}

void MainWindow::configureAdb() {
    m_adb.program = cfg->adbPath->text().trimmed();
    m_adb.serial = cfg->serial->text().trimmed();
}

void MainWindow::connectDebugActions() {
    connect(cfg->refresh, &QPushButton::clicked, this, [this] {
        configureAdb();
        m_adb.run(
            {"devices", "-l"},
            [this](bool ok, QByteArray out) {
                if (!ok)
                    return;
                log("USB 设备列表：" + QString::fromUtf8(out), "USB device list: " + QString::fromUtf8(out));
                QStringList found;
                for (auto line : out.split('\n')) {
                    auto parts = QString::fromUtf8(line).simplified().split(' ');
                    if (parts.size() >= 2 && parts[1] == "device")
                        found << parts[0];
                }
                if (found.size() == 1)
                    cfg->serial->setText(found[0]);
            },
            false);
    });
    connect(cfg->adbPreview, &QPushButton::clicked, this, &MainWindow::preview);
    connect(cfg->adbConnect, &QPushButton::clicked, this, [this] {
        configureAdb();
        m_adb.run(
            {"connect", cfg->adbEndpoint->text().trimmed()},
            [this](bool, QByteArray out) {
                log("无线 ADB：" + QString::fromUtf8(out), "Wireless ADB: " + QString::fromUtf8(out));
            },
            false);
    });
    connect(cfg->back, &QPushButton::clicked, this, [this] { key(4); });
    connect(cfg->home, &QPushButton::clicked, this, [this] { key(3); });
    connect(cfg->recent, &QPushButton::clicked, this, [this] { key(187); });
}

void MainWindow::refreshPairingHint() {
    const auto text = cfg->token->text();
    if (text.trimmed().isEmpty()) {
        cfg->tokenHint->clear();
        return;
    }
    const auto check = pairingCheckManualCode(text);
    cfg->tokenHint->setText(check.ok ? QString() : furinaStatus(check.chinese, check.english));
}

void MainWindow::bindPairingUi() {
    connect(cfg->token, &QLineEdit::textChanged, this, [this] { refreshPairingHint(); });
    connect(cfg->showQr, &QPushButton::clicked, this, &MainWindow::showPairingQr);
    m_qrExpiry.setParent(this);
    m_qrExpiry.setSingleShot(true);
    connect(&m_qrExpiry, &QTimer::timeout, this, [this] {
        if (!m_oneTimeListen)
            return;
        log("二维码已过期。", "QR code has expired.");
        retireOneTimeListener();
    });
}

void MainWindow::clearPairingQr() {
    cfg->qrImage->clear();
    cfg->qrHint->setText("二维码由本机显示，平板用相机扫描。约 2 分钟内有效，只能使用一次。\n"
                         "EN: The PC shows the QR code. Scan it with the tablet camera. "
                         "It is valid for about two minutes and can be used once.");
    m_qrExpiry.stop();
}

void MainWindow::retireOneTimeListener() {
    const auto secret = m_oneTimeSecret;
    const auto expiry = m_oneTimeExpiry;
    const bool listening = m_oneTimeListen;
    m_oneTimeListen = false;
    m_oneTimeSecret.clear();
    m_oneTimeExpiry = 0;
    clearPairingQr();
    if (!secret.isEmpty())
        pairingConsumeOneTimeSecret(SecretStore::instance(), secret, expiry, QDateTime::currentSecsSinceEpoch());
    if (!listening || m_peer.ready())
        return;
    m_peer.close();
    m_pending = false;
    m_castUi->connectionLost();
}

void MainWindow::showPairingQr() {
    if (m_castUi->locked() && !m_oneTimeListen) {
        log("投屏方式已锁定，请点击停止后修改设置。",
            "Casting options are locked. Press Stop to change settings.");
        return;
    }
    const auto hosts = pairingLocalIpv4();
    const auto selected = cfg->qrHost->currentText();
    cfg->qrHost->clear();
    cfg->qrHost->addItems(hosts);
    const auto keep = cfg->qrHost->findText(selected);
    if (keep >= 0)
        cfg->qrHost->setCurrentIndex(keep);
    if (hosts.isEmpty()) {
        clearPairingQr();
        log("没有可用于配对的本机 IPv4 地址。", "No local IPv4 address is available for pairing.");
        return;
    }
    auto& store = SecretStore::instance();
    const auto publicKey = pairingDesktopPublicKey(store);
    const auto fingerprint = pairingPublicKeyFingerprint(publicKey);
    const auto secret = pairingGenerateOneTimeSecret();
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (publicKey.size() != 32 || fingerprint.size() != 64 || secret.isEmpty()) {
        log("无法生成配对二维码。", "Could not create a pairing QR code.");
        return;
    }
    PairingPayload payload;
    payload.host = cfg->qrHost->currentText();
    payload.port = kPairingQrPort;
    payload.secret = secret;
    payload.fingerprint = fingerprint;
    payload.expiry = now + kPairingQrTtlSeconds;
    const auto text = pairingEncodePayload(payload);
    const auto image = pairingQrImage(text);
    if (text.isEmpty() || image.isNull()) {
        log("无法生成配对二维码。", "Could not create a pairing QR code.");
        return;
    }
    PairingCredential credential;
    credential.secret = secret;
    credential.kind = PairingSecretKind::OneTimeQr;
    credential.pinnedFingerprint = fingerprint;
    if (!m_oneTimeSecret.isEmpty())
        pairingConsumeOneTimeSecret(store, m_oneTimeSecret, m_oneTimeExpiry, now);
    if (!pairingListen(m_peer, payload.port, credential, payload.host)) {
        log("配对二维码暂时无法监听。", "The pairing QR code could not listen.");
        return;
    }
    m_oneTimeSecret = secret;
    m_oneTimeExpiry = payload.expiry;
    m_oneTimeListen = true;
    cfg->qrImage->setPixmap(QPixmap::fromImage(image));
    if (!m_castUi->locked())
        m_castUi->lockPending();
    m_pending = true;
    m_qrExpiry.start(static_cast<int>(kPairingQrTtlSeconds * 1000));
    log("已生成配对二维码，等待平板扫描。", "Created a pairing QR code. Waiting for the tablet to scan it.");
}
