#include "mainwindow.h"
#include "screen_scale_settings.h"
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

void MainWindow::stopSharing() {
    if (m_cursorStream)
        m_cursorStream->stop();
    m_videoSender.setSeparateCursor(false);
    m_pointer.stop();
    if (m_sharing) {
        m_peer.sendStatus(furinaStatus("电脑屏幕共享已停止", "PC screen sharing stopped"));
        m_position->rememberCurrent();
    }
    m_position->reset();
    m_sharing = false;
    m_layoutRefresh.stop();
    m_capture.stop();
    m_videoSender.stop();
    m_captureScreen.clear();
    m_captureName.clear();
    m_virtual.release();
}

fr_cast_mode MainWindow::castMode() const {
    return ui->extend->isChecked() ? FR_CAST_EXTEND : FR_CAST_DUPLICATE;
}

bool MainWindow::usableScreen(QScreen* screen) const {
    if (!screen || !QGuiApplication::screens().contains(screen))
        return false;
    auto display = [](QScreen* s) {
        const auto r = s->geometry();
        return fr_cast_display{r.x(), r.y(), r.width(), r.height(), s == QGuiApplication::primaryScreen()};
    };
    const auto d = display(screen);
    auto* primary = QGuiApplication::primaryScreen();
    const auto p = primary ? display(primary) : fr_cast_display{};
    return fr_cast_source_usable(castMode(), &d, primary ? &p : nullptr);
}

QScreen* MainWindow::selectedScreen() const {
    if (castMode() == FR_CAST_DUPLICATE) {
        auto* primary = QGuiApplication::primaryScreen();
        return usableScreen(primary) ? primary : nullptr;
    }
    for (auto* screen : QGuiApplication::screens())
        if (usableScreen(screen) &&
            (QGuiApplication::platformName() == "offscreen" || screen->name() == m_virtual.screenName() ||
             screen->name() == m_virtual.deviceName()))
            return screen;
    return nullptr;
}

void MainWindow::observeScreen(QScreen* screen) {
    connect(screen, &QScreen::geometryChanged, this, &MainWindow::displayChanged, Qt::UniqueConnection);
}

void MainWindow::displayChanged() {
    refreshScreens();
    if (m_sharing && QGuiApplication::platformName() != "offscreen")
        m_layoutRefresh.start();
}

void MainWindow::rebindCapture() {
    if (!m_sharing || !m_peer.ready())
        return;
    // Qt can replace QScreen objects during one Windows display-change transaction.
    // Keep the monitor identity and the peer session, rather than detaching the virtual screen.
    for (auto* screen : QGuiApplication::screens())
        if (screen->name() == m_captureName && usableScreen(screen)) {
            m_captureScreen = screen;
            break;
        }
    const QString device = castMode() == FR_CAST_EXTEND ? m_virtual.deviceName() : m_captureName;
    if (!device.isEmpty()) {
        m_videoSender.refreshDisplay(device);
        m_cursorStream->setDisplay(device);
        if (castMode() == FR_CAST_EXTEND)
            m_pointer.setDisplay(device);
    }
    m_position->rememberCurrent();
    log("显示器布局已变更，正在重新绑定采集并恢复窗口画面。",
        "Display layout changed. Rebinding capture and restoring live windows.");
}

void MainWindow::refreshScreens() {
    if (m_sharing)
        if (auto* source = selectedScreen()) {
            m_captureScreen = source;
            m_captureName = source->name();
        }
    if (m_sharing && !m_captureScreen && QGuiApplication::platformName() != "offscreen")
        m_layoutRefresh.start();
}

bool MainWindow::startSharing() {
    if (!m_peer.ready() || !m_consent->active() || !m_consent->sharing())
        return false;
    if (castMode() == FR_CAST_EXTEND && QGuiApplication::platformName() != "offscreen") {
        // Prefer a dedicated tablet desktop, including when physical monitors exist.
        if (m_virtual.screenName().isEmpty()) {
            m_virtual.ensure(m_consent->portrait());
            return m_virtual.preparing() || m_sharing;
        }
        if (m_virtual.preparing())
            return true;
        if (!m_position->configured()) {
            m_position->begin();
            return true;
        }
        refreshScreens();
    }
    auto* source = selectedScreen();
    if (!source) {
        log("扩展暂不可用：请先启用第二块或虚拟显示器。",
            "Extend is unavailable. Enable a second or virtual display first.");
        return false;
    }
    m_sharing = true;
    m_captureScreen = source;
    m_captureName = source->name();
    if (QGuiApplication::platformName() == "offscreen")
        m_capture.start();
    else {
        m_videoSender.setResolution(m_resolution->value());
        const QString device = castMode() == FR_CAST_EXTEND ? m_virtual.deviceName() : source->name();
        m_videoSender.start(device, source->geometry());
        m_cursorStream->setDisplay(device);
        updateCursorMapping();
        if (castMode() == FR_CAST_EXTEND)
            m_pointer.setDisplay(device);
    }
    const auto cn =
        QString("电脑%1投屏已启动：%2").arg(castMode() == FR_CAST_EXTEND ? "扩展" : "复制", source->name());
    const auto en =
        QString("PC %1 sharing started: %2")
            .arg(castMode() == FR_CAST_EXTEND ? "extended desktop" : "duplicate screen", source->name());
    log(cn, en);
    m_peer.sendSession(castMode() == FR_CAST_EXTEND ? FR_EXTEND : FR_DUPLICATE);
    m_peer.sendStatus(furinaStatus(cn, en));
    return true;
}

void MainWindow::preview() {
    if (m_castUi->locked())
        return;
    stop();
    {
        const QSignalBlocker a(ui->share), b(ui->receive);
        ui->share->setChecked(false);
        ui->receive->setChecked(true);
    }
    ui->modePanel->hide();
    m_view->setInteractive(true);
    configureAdb();
    if (m_adb.serial.isEmpty()) {
        log("请先刷新 USB 设备码。", "Refresh the USB device serial first.");
        return;
    }
    m_adbOnly = true;
    m_castUi->lockPending();
    m_castUi->occupy(false, false);
    m_preview.start();
    log("ADB 预览启动，每 500ms 请求一帧。", "ADB preview started, requesting a frame every 500 ms.");
}

void MainWindow::gesture(fr_gesture g) {
    if (!ui->receive->isChecked())
        return;
    if (!m_adbOnly) {
        m_peer.sendGesture(g);
        return;
    }
    if (!m_deviceSize.isValid())
        return;
    auto x = [this](uint32_t n) { return QString::number(qRound(n / 65535.0 * (m_deviceSize.width() - 1))); };
    auto y = [this](uint32_t n) {
        return QString::number(qRound(n / 65535.0 * (m_deviceSize.height() - 1)));
    };
    const bool tap =
        qAbs(int(g.x1) - int(g.x2)) < 350 && qAbs(int(g.y1) - int(g.y2)) < 350 && g.duration_ms < 450;
    if (tap)
        m_adb.run({"shell", "input", "tap", x(g.x1), y(g.y1)});
    else
        m_adb.run(
            {"shell", "input", "swipe", x(g.x1), y(g.y1), x(g.x2), y(g.y2), QString::number(g.duration_ms)});
}

void MainWindow::key(quint32 k) {
    if (m_adbOnly)
        m_adb.run({"shell", "input", "keyevent", QString::number(k)});
    else
        m_peer.sendKey(k);
}

void MainWindow::updateCursorMapping() {
    if (!m_cursorStream)
        return;
    const bool enabled = m_sharing && m_independentCursor && m_peer.cursorReady();
    m_cursorStream->setEnabled(enabled);
    m_videoSender.setSeparateCursor(enabled && m_cursorStream->hasShape());
}

void MainWindow::connectVideoEvents() {
    m_displaySample.start();
    connect(m_resolution, &ResolutionChoice::changed, this, [this](int edge) {
        m_videoSender.setResolution(edge);
        m_peer.sendProfile(edge);
        saveSettings();
    });
    connect(&m_peer, &Peer::profile, this, [this](int edge) {
        const QSignalBlocker block(m_resolution);
        m_resolution->setValue(edge);
        m_videoSender.setResolution(edge);
        saveSettings();
    });
    connect(&m_peer, &Peer::feedback, this, [this](double fps, double ms, unsigned queue, double shown) {
        m_videoSender.feedback(qMin(fps, shown), queue, ms);
    });
    connect(&m_videoSender, &DesktopVideoSender::packet, this, [this](const QByteArray& packet) {
        if (m_sharing && m_consent->active() && m_consent->sharing())
            m_videoSender.accepted(m_peer.sendVideo(packet));
    });
    connect(&m_videoSender, &DesktopVideoSender::status, this, [this](const QString& text) { log(text); });
    connect(&m_videoSender, &DesktopVideoSender::failed, this, [this] {
        if (!m_sharing)
            return;
        stopSharing();
        m_sessionActive = ui->receive->isChecked();
        m_peer.sendSession(FR_STOP_RECEIVE);
        log("屏幕采集未能恢复，请点击停止，检查显示器后重新开始。",
            "Screen capture could not recover. Press Stop, check the monitor, then start again.");
    });
    m_layoutRefresh.setSingleShot(true);
    m_layoutRefresh.setInterval(250);
    connect(&m_layoutRefresh, &QTimer::timeout, this, &MainWindow::rebindCapture);
    connect(&m_videoSender, &DesktopVideoSender::stats, this, [this](double fps, double ms, int edge) {
        m_encodeWorkMs = ms;
        log(QString("电脑发送：帧率 %1 帧/秒，编码耗时 %2 毫秒，最长边 %3 像素，目标帧率 60 帧/秒")
                .arg(fps, 0, 'f', 1)
                .arg(ms, 0, 'f', 1)
                .arg(edge),
            QString("PC send: %1 FPS, encode %2 ms, edge %3, target 60")
                .arg(fps, 0, 'f', 1)
                .arg(ms, 0, 'f', 1)
                .arg(edge));
    });
    connect(&m_peer, &Peer::video, this, [this](const QByteArray& packet) {
        if (m_consent->active() && !m_consent->sharing() && ui->receive->isChecked())
            m_videoReceiver.submit(packet);
    });
    connect(&m_videoReceiver, &DesktopVideoReceiver::frameReady, this, [this] {
        QImage image;
        if (m_videoReceiver.takeImage(&image) && m_consent->active() && !m_consent->sharing() &&
            ui->receive->isChecked()) {
            if (!m_view->hasImage()) {
                m_displaySample.restart();
                m_view->takeDisplayedFrames();
            }
            m_view->setImage(image);
        }
    });
    connect(&m_videoReceiver, &DesktopVideoReceiver::recover, this,
            [this] { m_peer.sendSession(FR_REQUEST_KEY); });
    connect(&m_videoReceiver, &DesktopVideoReceiver::status, this,
            [this](const QString& text) { log(text); });
    connect(
        &m_videoReceiver, &DesktopVideoReceiver::stats, this, [this](double fps, double ms, unsigned queue) {
            const auto displayed = m_view->takeDisplayedFrames();
            const auto elapsed = qMax<qint64>(1, m_displaySample.restart());
            const double shown = m_view->isVisible() && !isMinimized() ? displayed * 1000.0 / elapsed : fps;
            m_videoSender.setReceiverWork(m_sharing ? ms : 0);
            m_peer.sendFeedback(fps, ms + (m_sharing ? m_encodeWorkMs : 0), queue, shown);
            log(QString("电脑接收：解码帧率 %1 帧/秒，显示帧率 %2 帧/秒，处理耗时 %3 毫秒")
                    .arg(fps, 0, 'f', 1)
                    .arg(shown, 0, 'f', 1)
                    .arg(ms, 0, 'f', 1),
                QString("PC receive: decode %1 FPS, display %2 FPS, processing %3 ms")
                    .arg(fps, 0, 'f', 1)
                    .arg(shown, 0, 'f', 1)
                    .arg(ms, 0, 'f', 1));
        });
    connect(&m_virtual, &VirtualDisplayController::status, this,
            [this](const QString& cn, const QString& en) { log(cn, en); });
    connect(&m_virtual, &VirtualDisplayController::failed, this,
            [this](const QString& cn, const QString& en) {
                log(cn, en);
                m_sessionActive = ui->receive->isChecked();
                m_peer.sendSession(FR_STOP_RECEIVE);
                refreshScreens();
            });
    connect(&m_virtual, &VirtualDisplayController::ready, this, [this](const QString&) {
        if (!m_peer.ready() || !ui->share->isChecked() || castMode() != FR_CAST_EXTEND) {
            m_virtual.release();
            return;
        }
        refreshScreens();
        m_sessionActive = startSharing() || ui->receive->isChecked();
    });
}

void MainWindow::setupInput() {
    m_cursorStream =
        new DesktopCursorStream(this, [this](const QByteArray& data) { m_peer.sendCursor(data); });
    m_cursorStream->shapeChanged = [this] { updateCursorMapping(); };
    connect(&m_peer, &Peer::cursorConnection, this, [this](bool ready) {
        updateCursorMapping();
        log(ready ? "独立鼠标通道已连接。" : "独立鼠标通道已断开，鼠标沿用视频合成。",
            ready ? "Independent cursor channel connected."
                  : "Cursor channel disconnected; using video composition.");
    });
    connect(&m_videoSender, &DesktopVideoSender::cursorShape, this, [this](const QByteArray& shape) {
        m_cursorStream->setShape(shape);
        updateCursorMapping();
    });
    auto* screenScale = new ScreenScaleSettings(&m_peer, this, true);
    m_virtual.setScalePercent(screenScale->percent());
    connect(screenScale, &ScreenScaleSettings::changed, &m_virtual,
            &VirtualDisplayController::setScalePercent);
    connect(&m_virtual, &VirtualDisplayController::scaleApplied, screenScale,
            &ScreenScaleSettings::confirmApplied);
    connect(&m_virtual, &VirtualDisplayController::scaleChanged, this, [this] {
        if (m_sharing)
            m_layoutRefresh.start();
    });
    auto* pointerSettings = new PointerSettings(&m_peer, this, true);
    m_independentCursor = pointerSettings->independentCursor();
    connect(pointerSettings, &PointerSettings::independentCursorChanged, this, [this](bool enabled) {
        m_independentCursor = enabled;
        updateCursorMapping();
        log(enabled ? "已启用独立映射鼠标。" : "已切换为鼠标与屏幕合成传输。",
            enabled ? "Independent cursor mapping enabled." : "Cursor is composited into the video.");
    });
    m_pointer.setMultiplier(pointerSettings->multiplier());
    connect(pointerSettings, &PointerSettings::changed, &m_pointer, &DesktopPointer::setMultiplier);
    connect(&m_pointer, &DesktopPointer::status, this, [this](const QString& text) { log(text); });
    auto* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(ui->duplicate);
    modeGroup->addButton(ui->extend);
    modeGroup->setExclusive(true);
    QSettings s;
    if (s.value("castMode", 0).toInt() == 1)
        ui->extend->setChecked(true);
}

void MainWindow::connectDisplayEvents() {
    auto modeChanged = [this](bool checked) {
        if (m_castUi->locked()) {
            m_castUi->preserveSelection();
            return;
        }
        if (!checked)
            return;
        if (m_sessionActive || m_consent->pending())
            stop();
        refreshScreens();
    };
    connect(ui->duplicate, &QCheckBox::toggled, this, modeChanged);
    connect(ui->extend, &QCheckBox::toggled, this, modeChanged);
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        observeScreen(screen);
        displayChanged();
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, &MainWindow::displayChanged);
    connect(qApp, &QGuiApplication::primaryScreenChanged, this, &MainWindow::displayChanged);
    for (auto* screen : QGuiApplication::screens())
        observeScreen(screen);
    refreshScreens();
    directionChanged();
}

void MainWindow::setupCaptureTimers() {
    m_capture.setTimerType(Qt::PreciseTimer);
    m_capture.setInterval(16);
    connect(&m_capture, &QTimer::timeout, this, [this] {
        if (!usableScreen(m_captureScreen)) {
            stopSharing();
            m_sessionActive = false;
            log("投屏显示器不可用，共享已停止。", "Capture display is unavailable. Sharing stopped.");
            refreshScreens();
            return;
        }
        if (m_peer.canSendFrame()) {
            const auto image = m_captureScreen->grabWindow(0).toImage();
            m_peer.sendFrame(image);
        }
    });
    m_preview.setInterval(500);
    connect(&m_preview, &QTimer::timeout, this, [this] {
        if (m_screenshot.state() == QProcess::NotRunning)
            m_screenshot.start(m_adb.program, {"-s", m_adb.serial, "exec-out", "screencap", "-p"});
    });
    m_screenshotTimeout.setSingleShot(true);
    m_screenshotTimeout.setInterval(10000);
    connect(&m_screenshot, &QProcess::started, &m_screenshotTimeout, qOverload<>(&QTimer::start));
    connect(&m_screenshotTimeout, &QTimer::timeout, this, [this] {
        log("ADB 截图超时", "ADB screenshot timed out");
        m_screenshot.kill();
    });
    connect(&m_screenshot, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus exit) {
                m_screenshotTimeout.stop();
                const auto out = m_screenshot.readAllStandardOutput();
                const auto error = m_screenshot.readAllStandardError();
                if (!m_adbOnly)
                    return;
                if (code || exit != QProcess::NormalExit) {
                    log("ADB 截图失败：" + QString::fromUtf8(error),
                        "ADB screenshot failed: " + QString::fromUtf8(error));
                    m_preview.stop();
                    return;
                }
                const QImage image = QImage::fromData(out, "PNG");
                if (!image.isNull()) {
                    m_deviceSize = image.size();
                    m_view->setImage(image);
                }
            });
    connect(&m_screenshot, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        log("ADB 启动失败：" + m_screenshot.errorString(),
            "ADB could not start: " + m_screenshot.errorString());
        m_preview.stop();
        m_screenshotTimeout.stop();
    });
}
