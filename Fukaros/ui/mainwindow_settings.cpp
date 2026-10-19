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
    cfg->token->setText(s.value("token").toString());
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
    QSettings s;
    s.setValue("resolution", m_resolution->value());
    s.setValue("adb", cfg->adbPath->text());
    s.setValue("serial", cfg->serial->text());
    s.setValue("host", cfg->host->text());
    s.setValue("token", cfg->token->text());
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
