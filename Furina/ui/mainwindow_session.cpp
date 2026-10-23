#include "mainwindow.h"
#include "cast_direction_settings.h"
#include "ui_mainwindow.h"
#include "ui_settings.h"
#include "settings_support.h"
#include <QSettings>
#include <QRandomGenerator>
#include <QNetworkInterface>
#include <QGuiApplication>
#include <QScreen>
#include <QSignalBlocker>
#include <QResizeEvent>
#include <QSysInfo>
#include <QButtonGroup>

void MainWindow::start() {
    if (m_castUi->locked())
        return;
    if (!ui->share->isChecked() && !ui->receive->isChecked()) {
        log("请先勾选分享或获取屏幕。", "Select Share or Receive first.");
        return;
    }
    if (m_peer.ready()) {
        m_pending = false;
        requestDirection();
        return;
    }
    if (cfg->token->text().size() < 6) {
        log("请填写至少 6 位的配对码。", "Enter a pairing code with at least 6 characters.");
        openSettings();
        return;
    }
    if (!m_listening) {
        const auto bind = cfg->connectionTabs->currentIndex() == 0 ? QString("0.0.0.0") : cfg->host->text();
        m_listening = m_peer.listen(27183, cfg->token->text(), bind);
        if (!m_listening)
            return;
        if (!m_discovery.start(bind))
            log("自动发现不可用，请在电脑手填平板 USB IP。",
                "Discovery is unavailable. Enter the tablet USB IP on the PC.");
        refreshAddresses();
        saveSettings();
    }
    m_pending = true;
    m_castUi->lockPending();
    log("开始等待对端同意。", "Starting: waiting for the other device to approve.");
    log("服务已启动，等待电脑连接。", "Service started. Waiting for the PC connection.");
}

void MainWindow::stop() {
    m_presentationSync->reset();
    m_android.keepSession(false);
    m_android.setExtensionOrientation(-1);
    m_discovery.stop();
    m_castUi->reset();
    m_consent->cancel();
    m_pending = false;
    m_sessionActive = false;
    m_peer.sendSession(FR_STOP_SHARE);
    m_peer.sendSession(FR_STOP_RECEIVE);
    m_peer.sendSession(FR_RESTORE);
    stopCapture();
    m_peer.close(true);
    m_listening = false;
    m_remoteMax = false;
    m_videoVisible = false;
    m_android.resetVideo();
    m_videoReceiver.reset();
    m_presentation->setActive(false);
    m_view->setImage({});
    furinaSetMaximizeIcon(ui->maximize, false);
}

void MainWindow::directionChanged() {
    if (m_castUi->locked()) {
        m_castUi->preserveSelection();
        return;
    }
    furinaSaveCastDirection(m_direction->sharing());
    ui->modePanel->setVisible(ui->receive->isChecked());
    m_videoVisible = false;
    m_android.resetVideo();
    m_videoReceiver.reset();
    m_view->setImage({});
    m_presentation->setActive(false);
    if (m_remoteMax) {
        m_peer.sendSession(FR_RESTORE);
        m_remoteMax = false;
        furinaSetMaximizeIcon(ui->maximize, false);
    }
    if (m_sessionActive || m_pending || m_consent->pending())
        stop();
    m_sessionActive = false;
}

void MainWindow::endSession() {
    m_android.keepSession(false);
    m_android.setExtensionOrientation(-1);
    m_castUi->reset();
    stopCapture();
    m_videoVisible = false;
    m_android.resetVideo();
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
    m_pending = false;
    m_peer.sendProfile(m_resolution->value());
    m_sessionActive = ui->share->isChecked() || ui->receive->isChecked();
    m_peer.sendSession(ui->share->isChecked() ? FR_START_RECEIVE : FR_STOP_RECEIVE);
    if (ui->share->isChecked())
        startCapture();
    else
        stopCapture();
    if (ui->receive->isChecked()) {
        m_peer.sendSession(ui->extend->isChecked() ? FR_EXTEND : FR_DUPLICATE);
        m_peer.sendSession(FR_START_SHARE);
        log("已请求电脑屏幕，等待画面。", "Requested the PC screen. Waiting for frames.");
    } else
        m_peer.sendSession(FR_STOP_SHARE);
}

void MainWindow::session(quint32 action) {
    if (action == FR_REQUEST_KEY) {
        if (m_sharing)
            m_android.requestKeyframe();
    } else if (action == FR_START_SHARE) {
        if (!m_consent->active() || !m_consent->sharing())
            return;
        m_pending = false;
        m_sessionActive = true;
        startCapture();
    } else if (action == FR_STOP_SHARE) {
        stopCapture();
    } else if (action == FR_START_RECEIVE) {
        if (!m_consent->active() || m_consent->sharing())
            return;
        m_pending = false;
        m_sessionActive = true;
    } else if (action == FR_STOP_RECEIVE) {
        m_view->setImage({});
        m_videoVisible = false;
        m_android.resetVideo();
        m_videoReceiver.reset();
    } else if (action == FR_DUPLICATE || action == FR_EXTEND) {
        if (m_castUi->occupied() && ui->extend->isChecked() != (action == FR_EXTEND)) {
            log("投屏方式已锁定，忽略会话中的模式更改。",
                "Casting options are locked. Ignoring an in-session mode change.");
            return;
        }
        if (!m_consent->active() || m_consent->sharing())
            return;
        const QSignalBlocker a(ui->duplicate), b(ui->extend);
        (action == FR_EXTEND ? ui->extend : ui->duplicate)->setChecked(true);
    }
}

void MainWindow::connectSessionEvents() {
    connect(m_consent, &SessionConsent::status, this,
            [this](const QString& cn, const QString& en) { log(cn, en); });
    connect(m_consent, &SessionConsent::incoming, this, [this] {
        m_castUi->lockPending();
        m_pending = false;
        m_presentation->setActive(false);
    });
    connect(m_consent, &SessionConsent::ended, this, &MainWindow::endSession);
    connect(m_consent, &SessionConsent::approved, this, [this](bool sharing, bool extended, bool initiatedHere) {
        m_android.keepSession(true);
        m_castUi->occupy(sharing, extended);
        log("投屏方式已锁定，清晰度与通用设置可调整；点击停止后可修改方式。",
            "Casting options are locked; resolution and general settings can still change. Press Stop to change the casting mode.");
        m_pending = false;
        m_direction->setSharing(sharing);
        {
            const QSignalBlocker a(ui->duplicate), b(ui->extend);
            (extended ? ui->extend : ui->duplicate)->setChecked(true);
        }
        ui->modePanel->setVisible(!sharing);
        m_sessionActive = true;
        if (!sharing && extended) {
            m_android.setExtensionOrientation(m_consent->portrait() ? 1 : 0);
            log(m_consent->portrait() ? "竖屏扩展已锁定；点击停止后恢复旋转。"
                                      : "横屏扩展已锁定；点击停止后恢复旋转。",
                m_consent->portrait() ? "Portrait extension locked. Press Stop to restore rotation."
                                      : "Landscape extension locked. Press Stop to restore rotation.");
        }
        m_layout->refresh(m_presentation->active());
        updateVideoGeometry();
        if (initiatedHere)
            applyDirection();
    });
}

void MainWindow::connectTransportEvents() {
    connect(&m_peer, &Peer::status, this, [this](const QString& message) { log(message); });
    connect(&m_android, &AndroidBridge::status, this, [this](const QString& message) {
        log(message);
        m_peer.sendStatus(message);
    });
    connect(&m_peer, &Peer::frame, this, [this](const QImage& image) {
        if (m_consent->active() && !m_consent->sharing() && ui->receive->isChecked()) {
            m_view->setImage(image);
            m_sessionActive = true;
        }
    });
    connect(&m_android, &AndroidBridge::jpeg, this, [this](const QByteArray& jpeg) {
        if (m_sharing && m_consent->active() && m_consent->sharing())
            m_peer.sendJpeg(jpeg);
    });
    connect(&m_peer, &Peer::session, this, &MainWindow::session);
    connect(&m_peer, &Peer::gesture, this, [this](fr_gesture gesture) {
        if (cfg->allowControl->isChecked())
            m_android.gesture(gesture);
        else
            m_peer.sendStatus(
                furinaStatus("平板未允许远程控制", "Remote control is not allowed on the tablet"));
    });
    connect(&m_peer, &Peer::key, this, [this](quint32 key) {
        if (cfg->allowControl->isChecked() && (key == 3 || key == 4 || key == 187))
            m_android.key(key);
    });
    connect(&m_peer, &Peer::connected, this, [this] {
        m_peer.sendStatus(furinaStatus("平板已连接，设备码：" + cfg->serial->text(),
                                       "Tablet connected, device code: " + cfg->serial->text()));
        // The PC initiates a direction immediately. If it doesn't, honour a pending tablet Start.
        QTimer::singleShot(500, this, [this] {
            if (m_pending && m_peer.ready()) {
                m_pending = false;
                requestDirection();
            }
        });
    });
    connect(&m_peer, &Peer::disconnected, this, [this] {
        m_android.keepSession(false);
        m_castUi->connectionLost();
        if (m_castUi->occupied())
            log("连接已中断，请点击停止释放投屏方式。",
                "Connection lost. Press Stop to unlock casting options.");
        stopCapture();
        m_videoVisible = false;
        m_android.resetVideo();
        m_videoReceiver.reset();
        m_pending = false;
        m_sessionActive = false;
        m_remoteMax = false;
        m_presentation->setActive(false);
        furinaSetMaximizeIcon(ui->maximize, false);
        m_view->setImage({});
    });
    connect(&m_android, &AndroidBridge::captureStopped, this, [this] {
        if (!m_sharing)
            return;
        stopCapture();
        m_sessionActive = false;
        m_peer.sendSession(FR_STOP_SHARE);
    });
}
