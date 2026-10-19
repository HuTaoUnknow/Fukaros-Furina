#include "mainwindow.h"
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

void MainWindow::startCapture() {
    if (!m_peer.ready() || m_sharing || !m_consent->active() || !m_consent->sharing())
        return;
    m_sharing = true;
    m_remoteFps = 60;
    m_remoteQueue = 0;
    m_remoteWorkMs = 0;
    m_encodeWorkMs = 0;
    fr_video_policy_init(&m_videoPolicy, m_resolution->value());
    m_android.setCaptureResolution(m_videoPolicy.edge);
    m_android.startCapture();
    log("正在请求平板录屏授权。", "Requesting tablet screen capture permission.");
}

void MainWindow::stopCapture() {
    m_sharing = false;
    m_android.wantFrame = false;
    m_android.stopCapture();
}

void MainWindow::updateVideoGeometry() {
#ifdef Q_OS_ANDROID
    const auto pos = m_view->mapTo(this, QPoint());
    const qreal ratio = devicePixelRatioF();
    const QRect rect(qRound(pos.x() * ratio), qRound(pos.y() * ratio), qRound(m_view->width() * ratio),
                     qRound(m_view->height() * ratio));
    const bool visible =
        m_videoVisible && ui->receive->isChecked() && !m_settings->isVisible() && isVisible();
    const bool full = m_presentation->active();
    if (rect != m_nativeRect || visible != m_nativeVisible || full != m_nativeFull) {
        m_nativeRect = rect;
        m_nativeVisible = visible;
        m_nativeFull = full;
        m_android.updateVideoView(rect, visible, full);
    }
#endif
}

void MainWindow::connectVideoEvents() {
    fr_video_policy_init(&m_videoPolicy, 0);
    connect(m_resolution, &ResolutionChoice::changed, this, [this](int edge) {
        fr_video_policy_init(&m_videoPolicy, edge);
        m_android.setCaptureResolution(m_videoPolicy.edge);
        m_peer.sendProfile(edge);
        saveSettings();
    });
    connect(&m_peer, &Peer::profile, this, [this](int edge) {
        const QSignalBlocker block(m_resolution);
        m_resolution->setValue(edge);
        fr_video_policy_init(&m_videoPolicy, edge);
        m_android.setCaptureResolution(m_videoPolicy.edge);
        saveSettings();
    });
    connect(&m_peer, &Peer::feedback, this, [this](double fps, double ms, unsigned queue, double shown) {
        m_remoteWorkMs = ms;
        m_remoteFps = qMin(fps, shown);
        m_remoteQueue = queue;
    });
    connect(&m_android, &AndroidBridge::video, this, [this](const QByteArray& packet) {
        if (m_sharing && m_consent->active() && m_consent->sharing() && !m_peer.sendVideo(packet))
            m_android.requestKeyframe();
    });
    connect(&m_peer, &Peer::video, this, [this](const QByteArray& packet) {
        if (!m_consent->active() || m_consent->sharing() || !ui->receive->isChecked())
            return;
        m_videoVisible = true;
        m_sessionActive = true;
#ifdef Q_OS_ANDROID
        m_android.receiveVideo(packet);
        updateVideoGeometry();
#else
 m_videoReceiver.submit(packet);
#endif
    });
    connect(&m_android, &AndroidBridge::needKey, this, [this] { m_peer.sendSession(FR_REQUEST_KEY); });
    connect(&m_android, &AndroidBridge::exitFullscreen, this, [this] {
        m_presentation->setActive(false);
        updateVideoGeometry();
    });
    connect(&m_android, &AndroidBridge::stopRequested, this, &MainWindow::stop);
    connect(&m_android, &AndroidBridge::backRequested, this, [this] {
        if (m_settings->isVisible()) {
            m_settings->reject();
            return;
        }
        if (m_presentation->active()) {
            m_presentation->setActive(false);
            updateVideoGeometry();
            return;
        }
        saveSettings();
        m_android.backgroundTask();
    });
    connect(
        &m_android, &AndroidBridge::videoStats, this,
        [this](bool sender, double fps, double ms, unsigned queue, double shown, int w, int h,
               unsigned sampleMs) {
            if (sender)
                log(QString(
                        "平板编码：帧率 %1 帧/秒，图形处理耗时 %2 毫秒，分辨率 %3×%4 像素，目标帧率 60 帧/秒")
                        .arg(fps, 0, 'f', 1)
                        .arg(ms, 0, 'f', 1)
                        .arg(w)
                        .arg(h),
                    QString("Tablet encode: %1 FPS, GPU processing %2 ms, %3x%4, target 60")
                        .arg(fps, 0, 'f', 1)
                        .arg(ms, 0, 'f', 1)
                        .arg(w)
                        .arg(h));
            else
                log(QString(
                        "平板解码：解码帧率 %1 帧/秒，显示帧率 %2 帧/秒，分辨率 %3×%4 像素，目标帧率 60 帧/秒")
                        .arg(fps, 0, 'f', 1)
                        .arg(shown, 0, 'f', 1)
                        .arg(w)
                        .arg(h),
                    QString("Tablet decode: %1 FPS, display %2 FPS, %3x%4, target 60")
                        .arg(fps, 0, 'f', 1)
                        .arg(shown, 0, 'f', 1)
                        .arg(w)
                        .arg(h));
            if (sender) {
                m_encodeWorkMs = ms;
                if (fr_video_policy_sample_elapsed(
                        &m_videoPolicy, qMin(fps, m_remoteFps),
                        qMax(ms + (ui->receive->isChecked() ? m_decodeWorkMs : 0), m_remoteWorkMs),
                        qMax(m_peer.videoQueueMs(), m_remoteQueue), sampleMs)) {
                    m_android.setCaptureResolution(m_videoPolicy.edge);
                    log(QString("自动清晰度：最长边 %1").arg(m_videoPolicy.edge),
                        QString("Auto resolution: max edge %1").arg(m_videoPolicy.edge));
                }
            } else {
                m_decodeWorkMs = ms;
                m_peer.sendFeedback(fps, ms + (m_sharing ? m_encodeWorkMs : 0), queue, shown);
            }
        });
    connect(&m_videoReceiver, &DesktopVideoReceiver::frameReady, this, [this] {
        QImage image;
        if (m_videoReceiver.takeImage(&image) && m_consent->active() && !m_consent->sharing() &&
            ui->receive->isChecked())
            m_view->setImage(image);
    });
    connect(&m_videoReceiver, &DesktopVideoReceiver::recover, this,
            [this] { m_peer.sendSession(FR_REQUEST_KEY); });
    connect(&m_videoReceiver, &DesktopVideoReceiver::status, this,
            [this](const QString& text) { log(text); });
}

void MainWindow::setupCaptureTimers() {
    m_tick.setTimerType(Qt::PreciseTimer);
    m_tick.setInterval(16);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        m_android.wantFrame = m_sharing && m_peer.ready();
        updateVideoGeometry();
#ifndef Q_OS_ANDROID
        if (m_sharing && m_peer.canSendFrame()) {
            auto* screen = QGuiApplication::primaryScreen();
            if (screen)
                m_peer.sendFrame(screen->grabWindow(0).toImage());
        }
#endif
    });
    m_tick.start();
    directionChanged();
}
