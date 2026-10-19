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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), cfg(new Ui::SettingsDialog), m_settings(new QDialog(this)),
      m_peer(this), m_discovery(this), m_android(this) {
    setupInterface();
    connectSessionEvents();
    connectVideoEvents();
    initializeSettings();
    connectActions();
    connectTransportEvents();
    setupCaptureTimers();
    log("等待投屏。按开始启动服务，再由电脑配对连接。",
        "Waiting for a stream. Press Start to run the service, then pair from the PC.");
#ifndef Q_OS_ANDROID
    log("当前是桌面模拟，Android 录屏授权需在平板确认。",
        "Desktop simulation. Android screen capture permission must be confirmed on the tablet.");
#endif
}

MainWindow::~MainWindow() {
    m_tick.stop();
    stop();
    saveSettings();
    delete cfg;
    delete ui;
}

void MainWindow::log(const QString& message, const QString& english) {
    const auto entry = furinaLog(message, english);
    if (!entry.isEmpty())
        ui->log->appendPlainText(entry);
}

void MainWindow::maximize() {
    if (ui->share->isChecked()) {
        if (!m_peer.ready()) {
            log("请先连接电脑，再放大接收端画面。", "Connect the PC before maximizing its received content.");
            return;
        }
        if (!m_consent->sharing())
            return;
        const bool requested = m_presentationSync->toggleRemote();
        log(requested ? "已请求电脑全屏显示。" : "已请求电脑退出全屏。",
            requested ? "Requested full-screen viewing on the PC."
                      : "Requested leaving full-screen viewing on the PC.");
    } else
        m_presentation->setActive(!m_presentation->active());
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_layout)
        m_layout->refresh(m_presentation->active());
}

void MainWindow::setupInterface() {
    ui->setupUi(this);
    cfg->setupUi(m_settings);
    furinaConfigureSettingsScroll(cfg->settingsScroll);
    cfg->buttonBox->button(QDialogButtonBox::Save)->setText("保存");
    cfg->buttonBox->button(QDialogButtonBox::Cancel)->setText("取消");
    setStyleSheet(furinaStyle());
    furinaConfigureToolIcons(ui->settings, ui->maximize);
    setWindowIcon(QIcon(":/icons/app.png"));
    setWindowTitle("Furina · 平板端");
    resize(1100, 680);
    m_view = new ScreenView(this);
    ui->viewLayout->addWidget(m_view);
    m_presentation =
        new Presentation(this, ui->sidebar, ui->bottomPanel, m_view, Presentation::ExitControl::SystemBack);
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
    m_consent->setReceiverOrientation([this] { return width() < height(); });
    m_castUi = new CastUiState(this);
    m_castControl = new CastControl(this, m_castUi, &m_peer);
    m_layout = new MobileLayout(this);
}

void MainWindow::connectActions() {
    connect(m_castControl, &CastControl::startRequested, this, &MainWindow::start);
    connect(m_castControl, &CastControl::stopRequested, this, &MainWindow::stop);
    connect(ui->settings, &QPushButton::clicked, this, &MainWindow::openSettings);
    connect(ui->maximize, &QPushButton::clicked, this, &MainWindow::maximize);
    connect(m_presentation, &Presentation::changed, this, [this](bool on) {
        furinaSetMaximizeIcon(ui->maximize, on || m_remoteMax);
        m_layout->refresh(on);
        updateVideoGeometry();
    });
    connect(m_direction, &DirectionChoice::changed, this, &MainWindow::directionChanged);
    auto modeChanged = [this](bool checked) {
        if (m_castUi->locked()) {
            m_castUi->preserveSelection();
            return;
        }
        if (checked && (m_sessionActive || m_consent->pending()))
            stop();
    };
    connect(ui->duplicate, &QCheckBox::toggled, this, modeChanged);
    connect(ui->extend, &QCheckBox::toggled, this, modeChanged);
    connect(m_settings, &QDialog::accepted, this, [this] {
        if (m_castUi->locked()) {
            loadSettings();
            log("投屏方式已锁定，请点击停止后修改设置。",
                "Casting options are locked. Press Stop to change settings.");
            return;
        }
        stop();
        saveSettings();
        log("设置已保存，请按开始重新启动服务。", "Settings saved. Press Start to restart the service.");
    });
    connect(m_settings, &QDialog::rejected, this, &MainWindow::loadSettings);
    connect(cfg->usbSettings, &QPushButton::clicked, &m_android, &AndroidBridge::usbSettings);
    connect(cfg->refreshAddresses, &QPushButton::clicked, this, &MainWindow::refreshAddresses);
    connect(cfg->accessibility, &QPushButton::clicked, &m_android, &AndroidBridge::accessibility);
}
