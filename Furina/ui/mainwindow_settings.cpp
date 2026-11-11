#include "mainwindow.h"
#include "cast_direction_settings.h"
#include "debug_log.h"
#include "pairing_camera.h"
#include "pairing_code.h"
#include "pairing_identity.h"
#include "pairing_payload.h"
#include "pairing_session.h"
#include "screen_scale_settings.h"
#include "secret_store.h"
#include "ui_mainwindow.h"
#include "ui_settings.h"
#include "settings_support.h"
#include <QDateTime>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSettings>
#include <QVBoxLayout>
#include <QNetworkInterface>
#include <QGuiApplication>
#include <QScreen>
#include <QSignalBlocker>
#include <QResizeEvent>
#include <QSysInfo>
#include <QButtonGroup>

void MainWindow::loadSettings() {
    QSettings s;
    if (!m_castUi->locked()) {
        {
            const QSignalBlocker block(m_resolution);
            m_resolution->setValue(s.value("resolution", 0).toInt());
        }
        fr_video_policy_init(&m_videoPolicy, m_resolution->value());
        m_android.setCaptureResolution(m_videoPolicy.edge);
    }
    cfg->connectionTabs->setCurrentIndex(s.value("connectionTab", 0).toInt() == 1 ? 1 : 0);
    cfg->host->setText(s.value("host", "0.0.0.0").toString());
    cfg->serial->setText(s.value("serial", m_android.deviceCode()).toString());
    cfg->token->setText(QString::fromUtf8(
        pairingMigratePlaintext(SecretStore::instance(), s, QStringLiteral("token"), QStringLiteral("manual-code"))));
    refreshPairingHint();
    cfg->allowControl->setChecked(s.value("allowControl", false).toBool());
}

void MainWindow::saveSettings() {
    furinaSaveCastDirection(m_direction->sharing());
    QSettings s;
    s.setValue("resolution", m_resolution->value());
    s.setValue("connectionTab", cfg->connectionTabs->currentIndex());
    s.setValue("host", cfg->host->text());
    s.setValue("serial", cfg->serial->text());
    const auto code = pairingNormalizeManualCode(cfg->token->text());
    auto& secrets = SecretStore::instance();
    if (code.isEmpty())
        secrets.remove(QStringLiteral("manual-code"));
    else if (!secrets.store(QStringLiteral("manual-code"), code.toUtf8()))
        log("配对码未能写入安全存储。", "Could not store the pairing code.");
    s.remove(QStringLiteral("token"));
    s.setValue("allowControl", cfg->allowControl->isChecked());
    s.setValue("castMode", ui->extend->isChecked() ? 1 : 0);
}

void MainWindow::refreshAddresses() {
    QStringList usb, other;
    for (const auto& interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp))
            continue;
        const auto name = interface.name();
        const bool wired = name.startsWith("rndis") || name.startsWith("usb") || name.startsWith("ncm");
        for (const auto& entry : interface.addressEntries())
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.ip().isLoopback())
                (wired ? usb : other) << entry.ip().toString();
    }
    cfg->usbAddresses->setText(usb.isEmpty() ? "USB IP：未就绪" : "USB IP：" + usb.join(", "));
    cfg->addresses->setText("本机 IP：" + (other.isEmpty() ? QString("未连接") : other.join(", ")));
}

void MainWindow::openSettings() {
    refreshAddresses();
#ifdef Q_OS_ANDROID
    const auto size = ui->contentPanel->size();
    m_settings->resize(qMin(560, size.width() - 24), qMin(620, size.height() - 24));
    m_settings->move((size.width() - m_settings->width()) / 2, (size.height() - m_settings->height()) / 2);
    ui->sidebar->setEnabled(false);
    ui->portraitToolbar->setEnabled(false);
    ui->portraitTabs->setEnabled(false);
    ui->viewPanel->setEnabled(false);
    ui->bottomPanel->setEnabled(false);
    m_settings->show();
    m_settings->raise();
    updateVideoGeometry();
#else
    const auto size = screen()->availableGeometry().size();
    m_settings->resize(qMin(560, size.width() - 24), qMin(620, size.height() - 24));
    m_settings->open();
#endif
}

void MainWindow::initializeSettings() {
#ifdef Q_OS_ANDROID
    // Render settings on the existing activity surface rather than a secondary native window.
    m_settings->setParent(ui->contentPanel, Qt::Widget);
    m_settings->hide();
    connect(m_settings, &QDialog::finished, this, [this] {
        ui->sidebar->setEnabled(true);
        ui->portraitToolbar->setEnabled(true);
        ui->portraitTabs->setEnabled(true);
        updateVideoGeometry();
        ui->viewPanel->setEnabled(true);
        ui->bottomPanel->setEnabled(true);
    });
#endif
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive)
            furinaSaveCastDirection(m_direction->sharing());
        setUpdatesEnabled(state != Qt::ApplicationSuspended);
        if (state == Qt::ApplicationActive) {
            m_nativeRect = {};
            updateVideoGeometry();
            if (m_consent->active() && !m_consent->sharing() && m_peer.ready())
                m_peer.sendSession(FR_REQUEST_KEY);
        }
    });
    QSettings s;
    auto& store = SecretStore::instance();
    if (pairingMigratePlaintext(store, s, QStringLiteral("token"), QStringLiteral("manual-code")).isEmpty()) {
        const auto generated = pairingGenerateManualCode();
        if (!generated.isEmpty())
            store.store(QStringLiteral("manual-code"), generated.toUtf8());
    }
    loadSettings();
    new PointerSettings(&m_peer, this, false);
    new ScreenScaleSettings(&m_peer, this, false);
    connect(&m_peer, &Peer::cursor, this, [this](const QByteArray& data) {
        if (m_consent->active() && !m_consent->sharing())
            m_android.receiveCursor(data);
    });
    auto* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(ui->duplicate);
    modeGroup->addButton(ui->extend);
    modeGroup->setExclusive(true);
    if (s.value("castMode", 0).toInt() == 1)
        ui->extend->setChecked(true);
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
    connect(cfg->scanQr, &QPushButton::clicked, this, &MainWindow::scanPairingQr);
}

void MainWindow::scanPairingQr() {
    if (m_castUi->locked()) {
        log("投屏方式已锁定，请点击停止后修改设置。",
            "Casting options are locked. Press Stop to change settings.");
        return;
    }
#ifdef Q_OS_ANDROID
    pairingStartCameraScan(this, [this](const QString& payload) {
        if (payload.isEmpty()) {
            log("没有扫到配对二维码。", "No pairing QR code was scanned.");
            return;
        }
        pairFromQrPayload(payload);
    });
#else
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle("扫描二维码");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto* layout = new QVBoxLayout(dialog);
    auto* help = new QLabel(
        "桌面模拟没有平板相机。请把电脑上的配对内容粘贴到下面。平板会直接打开相机。\n"
        "EN: Desktop simulation has no tablet camera. Paste the pairing payload. The tablet opens the camera.",
        dialog);
    help->setWordWrap(true);
    auto* edit = new QPlainTextEdit(dialog);
    edit->setPlaceholderText("fukaros://pair?...");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    layout->addWidget(help);
    layout->addWidget(edit);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::accepted, this, [this, edit] { pairFromQrPayload(edit->toPlainText().trimmed()); });
    dialog->resize(520, 280);
    dialog->open();
#endif
}
