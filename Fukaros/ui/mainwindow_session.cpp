#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ui_settings.h"
#include "settings_support.h"
#include <QGuiApplication>
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

void MainWindow::start() {
    if (m_castUi->locked())
        return;
    if (!ui->share->isChecked() && !ui->receive->isChecked()) {
        log("请先勾选分享或获取屏幕。", "Select Share or Receive first.");
        return;
    }
    if (m_peer.ready()) {
        requestDirection();
        return;
    }
    if (m_pending)
        return;
    if (cfg->token->text().size() < 6) {
        log("请在设置中填写至少 6 位的配对码。",
            "Enter a pairing code with at least 6 characters in Settings.");
        openSettings();
        return;
    }
    stop();
    m_pending = true;
    m_castUi->lockPending();
    log("开始等待对端同意。", "Starting: waiting for the other device to approve.");
    saveSettings();
    const int generation = m_generation;
    if (cfg->transport->currentIndex() == 1) {
        QHostAddress address;
        if (!address.setAddress(cfg->host->text().trimmed()) ||
            address.protocol() != QAbstractSocket::IPv4Protocol) {
            m_pending = false;
            m_castUi->connectionLost();
            log("平板 IP 无效，请检查设置。", "Invalid tablet IP. Check Settings.");
            return;
        }
        log("正在通过 Wi-Fi 连接平板。", "Connecting to the tablet over Wi-Fi.");
        m_peer.connectTo(cfg->host->text().trimmed(), 27183, cfg->token->text());
    } else {
        if (cfg->usbMode->currentIndex() == 0) {
            m_usbNetwork.start(cfg->usbHost->text());
            return;
        }
        configureAdb();
        if (m_adb.program.isEmpty()) {
            m_pending = false;
            m_castUi->connectionLost();
            log("未找到 ADB，请在线下方设置路径。",
                "ADB was not found. Set its path below the separator in Settings.");
            return;
        }
        if (!m_adb.serial.isEmpty()) {
            connectUsb(generation);
            return;
        }
        m_adb.run(
            {"devices", "-l"},
            [this, generation](bool ok, QByteArray out) {
                if (generation != m_generation)
                    return;
                QStringList found;
                for (const auto& line : out.split('\n')) {
                    auto parts = QString::fromUtf8(line).simplified().split(' ');
                    if (parts.size() >= 2 && parts[1] == "device")
                        found << parts[0];
                }
                if (ok && found.size() == 1) {
                    cfg->serial->setText(found[0]);
                    configureAdb();
                    connectUsb(generation);
                } else {
                    m_pending = false;
                    m_castUi->connectionLost();
                    log("请授权 USB 调试，并在设置中选择设备码。",
                        "Authorize USB debugging and select a device serial in Settings.");
                }
            },
            false);
    }
}

void MainWindow::connectUsb(int generation) {
    m_adb.run(
        {"forward", "tcp:27183", "tcp:27183"},
        [this, generation](bool ok, QByteArray) {
            if (generation != m_generation)
                return;
            if (ok)
                m_peer.connectTo("127.0.0.1", 27183, cfg->token->text());
            else {
                m_pending = false;
                m_castUi->connectionLost();
            }
        },
        true);
}

void MainWindow::stop() {
    m_presentationSync->reset();
    m_castUi->reset();
    m_consent->cancel();
    m_usbNetwork.cancel();
    ++m_generation;
    m_peer.sendSession(FR_STOP_SHARE);
    m_peer.sendSession(FR_STOP_RECEIVE);
    m_peer.sendSession(FR_RESTORE);
    stopSharing();
    m_pending = false;
    m_sessionActive = false;
    m_adbOnly = false;
    m_remoteMax = false;
    m_deviceSize = {};
    m_preview.stop();
    m_screenshotTimeout.stop();
    m_screenshot.kill();
    m_screenshot.waitForFinished(1000);
    m_adb.cancel();
    m_peer.close(true);
    m_presentation->setActive(false);
    m_videoReceiver.reset();
    m_view->setImage({});
    furinaSetMaximizeIcon(ui->maximize, false);
}

void MainWindow::directionChanged() {
    if (m_castUi->locked()) {
        m_castUi->preserveSelection();
        return;
    }
    ui->modePanel->setVisible(ui->share->isChecked());
    m_view->setInteractive(ui->receive->isChecked());
    if (m_sessionActive || m_pending || m_consent->pending())
        stop();
    stopSharing();
    m_preview.stop();
    m_adbOnly = false;
    m_view->setImage({});
    m_presentation->setActive(false);
    if (m_remoteMax) {
        m_peer.sendSession(FR_RESTORE);
        m_remoteMax = false;
        furinaSetMaximizeIcon(ui->maximize, false);
    }
    m_sessionActive = false;
}

void MainWindow::endSession() {
    m_castUi->reset();
    stopSharing();
    m_videoReceiver.reset();
    m_view->setImage({});
    m_presentation->setActive(false);
    m_pending = false;
    m_sessionActive = false;
    m_remoteMax = false;
    furinaSetMaximizeIcon(ui->maximize, false);
}

void MainWindow::requestDirection() {
    if (m_castUi->occupied())
        return;
    if (m_consent->request(m_direction->sharing(), ui->extend->isChecked())) {
        m_pending = false;
        m_castUi->lockPending();
    }
}

void MainWindow::applyDirection() {
    if (!m_consent->active())
        return;
    m_sessionActive = false;
    m_peer.sendProfile(m_resolution->value());
    m_peer.sendSession(ui->share->isChecked() ? FR_START_RECEIVE : FR_STOP_RECEIVE);
    if (ui->share->isChecked())
        m_sessionActive = startSharing();
    else
        stopSharing();
    m_peer.sendSession(ui->receive->isChecked() ? FR_START_SHARE : FR_STOP_SHARE);
    if (ui->receive->isChecked()) {
        m_sessionActive = true;
        log("已请求平板屏幕，请在平板确认录屏授权。",
            "Requested tablet screen. Confirm screen capture permission on the tablet.");
    }
}

void MainWindow::session(quint32 action) {
    if (action == FR_REQUEST_KEY) {
        if (m_sharing)
            m_videoSender.requestKeyframe();
    } else if (action == FR_START_SHARE) {
        if (!m_consent->active() || !m_consent->sharing())
            return;
        m_pending = false;
        m_sessionActive = startSharing();
    } else if (action == FR_STOP_SHARE) {
        stopSharing();
    } else if (action == FR_START_RECEIVE) {
        if (!m_consent->active() || m_consent->sharing())
            return;
        m_pending = false;
        m_sessionActive = true;
    } else if (action == FR_STOP_RECEIVE) {
        m_videoReceiver.reset();
        m_view->setImage({});
    } else if (action == FR_DUPLICATE || action == FR_EXTEND) {
        if (m_castUi->occupied() && ui->extend->isChecked() != (action == FR_EXTEND)) {
            log("投屏方式已锁定，忽略会话中的模式更改。",
                "Casting options are locked. Ignoring an in-session mode change.");
            return;
        }
        if (!m_consent->active() || !m_consent->sharing())
            return;
        const QSignalBlocker a(ui->duplicate), b(ui->extend);
        (action == FR_EXTEND ? ui->extend : ui->duplicate)->setChecked(true);
        refreshScreens();
    }
}

void MainWindow::connectSessionEvents() {
    m_position = new DisplayPosition(
        this, [this](QPoint* position) { return m_virtual.position(position); },
        [this](const QPoint& position) { return m_virtual.setPosition(position); });
    connect(m_position, &DisplayPosition::status, this,
            [this](const QString& cn, const QString& en) { log(cn, en); });
    connect(m_position, &DisplayPosition::cancelled, this, &MainWindow::stop);
    connect(m_position, &DisplayPosition::ready, this, [this] {
        const int generation = m_generation;
        // SetDisplayConfig completes before Qt receives WM_DISPLAYCHANGE.
        QTimer::singleShot(200, this, [this, generation] {
            if (generation == m_generation && m_peer.ready() && m_consent->active() && m_consent->sharing() &&
                castMode() == FR_CAST_EXTEND && m_position->configured()) {
                refreshScreens();
                m_sessionActive = startSharing();
            }
        });
    });
    connect(m_consent, &SessionConsent::status, this,
            [this](const QString& cn, const QString& en) { log(cn, en); });
    connect(m_consent, &SessionConsent::incoming, this, [this] {
        m_castUi->lockPending();
        m_pending = false;
        m_presentation->setActive(false);
    });
    connect(m_consent, &SessionConsent::ended, this, &MainWindow::endSession);
    connect(m_consent, &SessionConsent::approved, this, [this](bool sharing, bool extended, bool initiatedHere) {
        m_castUi->occupy(sharing, extended);
        log("投屏方式已锁定，清晰度与通用设置可调整；点击停止后可修改方式。",
            "Casting options are locked; resolution and general settings can still change. Press Stop to change the casting mode.");
        m_pending = false;
        m_direction->setSharing(sharing);
        {
            const QSignalBlocker a(ui->duplicate), b(ui->extend);
            (extended ? ui->extend : ui->duplicate)->setChecked(true);
        }
        ui->modePanel->setVisible(sharing);
        m_sessionActive = true;
        m_view->setInteractive(!sharing);
        refreshScreens();
        if (initiatedHere)
            applyDirection();
    });
}

void MainWindow::connectTransportEvents() {
    connect(&m_peer, &Peer::status, this, [this](const QString& message) { log(message); });
    connect(&m_usbNetwork, &UsbNetwork::status, this,
            [this](const QString& cn, const QString& en) { log(cn, en); });
    connect(&m_usbNetwork, &UsbNetwork::failed, this, [this](const QString& cn, const QString& en) {
        m_pending = false;
        m_castUi->connectionLost();
        log(cn, en);
    });
    connect(&m_usbNetwork, &UsbNetwork::found, this,
            [this](const QString& host, const QString& local, const QString& name) {
                if (!m_pending)
                    return;
                log(QString("USB 网络直连：%1 → %2（%3），无需 USB 调试。").arg(local, host, name),
                    QString("Direct USB network: %1 -> %2 (%3). USB debugging is not required.")
                        .arg(local, host, name));
                m_peer.connectTo(host, 27183, cfg->token->text(), local);
            });
    connect(&m_adb, &Adb::status, this, [this](const QString& message) { log(message); });
    connect(&m_peer, &Peer::frame, this, [this](const QImage& image) {
        if (m_consent->active() && !m_consent->sharing() && ui->receive->isChecked())
            m_view->setImage(image);
    });
    connect(m_view, &ScreenView::gesture, this, &MainWindow::gesture);
    connect(&m_peer, &Peer::session, this, &MainWindow::session);
    connect(&m_peer, &Peer::connected, this, [this] {
        if (m_pending) {
            m_pending = false;
            requestDirection();
        }
    });
    connect(&m_peer, &Peer::disconnected, this, [this] {
        m_castUi->connectionLost();
        if (m_castUi->occupied())
            log("连接已中断，请点击停止释放投屏方式。",
                "Connection lost. Press Stop to unlock casting options.");
        stopSharing();
        m_videoReceiver.reset();
        m_pending = false;
        m_sessionActive = false;
        m_remoteMax = false;
        furinaSetMaximizeIcon(ui->maximize, false);
        m_view->setImage({});
        m_presentation->setActive(false);
    });
}
