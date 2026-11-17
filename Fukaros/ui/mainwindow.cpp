#include "mainwindow.h"
#include "cast_direction_settings.h"
#include "ui_mainwindow.h"
#include "ui_settings.h"
#include "settings_support.h"
#include "pairing_session.h"
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QHostAddress>
#include <QSignalBlocker>
#include <QDesktopServices>
#include <QUrl>
#include <QResizeEvent>
#include <QButtonGroup>
#include <QBoxLayout>
#include "desktop_tray.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), cfg(new Ui::SettingsDialog), m_settings(new QDialog(this)),
      m_peer(this), m_adb(this), m_usbNetwork(this), m_lanDiscovery(this) {
    setupInterface();
    m_direction->setSharing(furinaRestoreCastDirection(m_direction->sharing()));
    connectSessionEvents();
    connectVideoEvents();
    loadSettings();
    setupInput();
    connectActions();
    connectTransportEvents();
    connectDebugActions();
    connectDisplayEvents();
    setupCaptureTimers();
    log("等待投屏。请在设置中填写连接信息，再选择方向并按开始。",
        "Waiting for a stream. Configure connection settings, choose a direction, then press Start.");
}

MainWindow::~MainWindow() {
    saveSettings();
    stop();
    disconnect(&m_virtual, nullptr, this, nullptr);
    delete m_cursorStream;
    delete cfg;
    delete ui;
}

void MainWindow::log(const QString& message, const QString& english) {
    const auto entry = furinaLog(message, english);
    if (entry.isEmpty())
        return;
    ui->log->appendPlainText(entry);
    // The window log is the only record of why a cast failed on the user's
    // machine, and it is gone once the app closes. Mirror it to a file.
    static QFile file(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                          .filePath("fukaros.log"));
    static bool opened = file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    if (opened) {
        file.write(entry.toUtf8());
        file.write("\n");
        file.flush();
    }
}

void MainWindow::maximize() {
    if (ui->share->isChecked()) {
        if (!m_peer.ready()) {
            log("请先连接平板，再放大接收端画面。",
                "Connect the tablet before maximizing its received content.");
            return;
        }
        if (!m_consent->sharing())
            return;
        const bool requested = m_presentationSync->toggleRemote();
        log(requested ? "已请求平板全屏显示。" : "已请求平板退出全屏。",
            requested ? "Requested full-screen viewing on the tablet."
                      : "Requested leaving full-screen viewing on the tablet.");
    } else
        m_presentation->setActive(!m_presentation->active());
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (!ui->bottomPanel)
        return;
    const bool landscape = width() >= height();
    ui->bottomLayout->setDirection(landscape ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    ui->bottomPanel->setMinimumHeight(landscape ? 220 : 310);
    ui->bottomPanel->setMaximumHeight(landscape ? 220 : 310);
}

void MainWindow::setupInterface() {
    ui->setupUi(this);
    cfg->setupUi(m_settings);
    furinaConfigureSettingsScroll(cfg->settingsScroll);
    setStyleSheet(furinaStyle());
    // The settings dialog reads Cancel, Save (Cancel left, Save right). Applied
    // after the stylesheet because a style change rebuilds the button box layout.
    furinaOrderDialogSaveButton(m_settings);
    furinaConfigureToolIcons(ui->settings, ui->maximize);
    setWindowIcon(QIcon(":/icons/app.png"));
    setWindowTitle("Fukaros · 电脑端");
    resize(1180, 760);
    m_view = new ScreenView(this);
    ui->viewLayout->addWidget(m_view);
    m_view->setInteractive(true);
    m_presentation = new Presentation(this, ui->sidebar, ui->bottomPanel, m_view);
    m_resolution = new ResolutionChoice(this);
    m_direction = new DirectionChoice(this);
    m_consent = new SessionConsent(&m_peer, this);
    m_presentationSync = new PresentationSync(&m_peer, m_presentation, m_consent, this);
    connect(m_presentationSync, &PresentationSync::remoteChanged, this, [this](bool on) {
        m_remoteMax = on;
        furinaSetMaximizeIcon(ui->maximize, on || m_presentation->active());
    });
    connect(m_presentationSync, &PresentationSync::status, this,
            [this](const QString& cn, const QString& en) { log(cn, en); });
    m_castUi = new CastUiState(this);
    // Injected rather than linked: the session widget layer must not depend on the secret store.
    m_castControl = new CastControl(this, m_castUi, &m_peer, [this] { return pairingStoredKeyAvailable(m_peer); });
    bindPairingUi();
}

void MainWindow::connectActions() {
    connect(qApp, &QCoreApplication::aboutToQuit, this,
            [this] { furinaSaveCastDirection(m_direction->sharing()); });
    connect(m_castControl, &CastControl::startRequested, this, &MainWindow::start);
    connect(m_castControl, &CastControl::stopRequested, this, &MainWindow::stop);
    connect(ui->settings, &QPushButton::clicked, this, &MainWindow::openSettings);
    connect(ui->maximize, &QPushButton::clicked, this, &MainWindow::maximize);
    connect(m_presentation, &Presentation::changed, this,
            [this](bool on) { furinaSetMaximizeIcon(ui->maximize, on || m_remoteMax); });
    connect(m_direction, &DirectionChoice::changed, this, &MainWindow::directionChanged);
    auto* tray = new DesktopTray(this, windowIcon());
    connect(tray, &DesktopTray::hiddenToTray, this, &MainWindow::saveSettings);
    connect(tray, &DesktopTray::quitRequested, this, [this] {
        saveSettings();
        stop();
        QCoreApplication::quit();
    });
    connect(m_settings, &QDialog::accepted, this, [this] {
        if (m_castUi->locked()) {
            loadSettings();
            log("投屏方式已锁定，请点击停止后修改设置。",
                "Casting options are locked. Press Stop to change settings.");
            return;
        }
        stop();
        saveSettings();
        log("设置已保存，请按开始重新连接。", "Settings saved. Press Start to reconnect.");
    });
    connect(m_settings, &QDialog::rejected, this, [this] {
        loadSettings();
        endPairMatch();
    });
    connect(m_settings, &QDialog::accepted, this, &MainWindow::endPairMatch);
    connect(m_settings, &QDialog::finished, this, &MainWindow::endPairMatch);
}
