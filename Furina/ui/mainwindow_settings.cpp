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
    cfg->token->setText(s.value("token").toString());
    cfg->allowControl->setChecked(s.value("allowControl", false).toBool());
}

void MainWindow::saveSettings() {
    QSettings s;
    s.setValue("resolution", m_resolution->value());
    s.setValue("connectionTab", cfg->connectionTabs->currentIndex());
    s.setValue("host", cfg->host->text());
    s.setValue("serial", cfg->serial->text());
    s.setValue("token", cfg->token->text());
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
        setUpdatesEnabled(state != Qt::ApplicationSuspended);
        if (state == Qt::ApplicationActive) {
            m_nativeRect = {};
            updateVideoGeometry();
            if (m_consent->active() && !m_consent->sharing() && m_peer.ready())
                m_peer.sendSession(FR_REQUEST_KEY);
        }
    });
    QSettings s;
    if (s.value("token").toString().size() < 6)
        s.setValue("token", QString::number(QRandomGenerator::system()->bounded(100000000, 999999999)));
    loadSettings();
    new PointerSettings(&m_peer, this, false);
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
