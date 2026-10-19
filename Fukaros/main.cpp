#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>
#include <QStandardPaths>
#include <QMessageBox>
#include "mainwindow.h"
#include "settings_identity.h"
#include "single_instance.h"
#include "desktop_tray.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Furina");
    QCoreApplication::setApplicationName("Fukaros");
    QCoreApplication::setApplicationVersion("0.4.19");
    SingleInstance instance(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    const auto result = instance.start();
    if (result == SingleInstance::Activated)
        return 0;
    if (result == SingleInstance::Error) {
        QMessageBox::warning(nullptr, "Fukaros", "无法唤起已有窗口，请稍后重试。\n" + instance.errorString());
        return 1;
    }
    furinaMigrateSettings("white_furina");
    MainWindow w;
    QObject::connect(&instance, &SingleInstance::activateRequested, &w, [&w] {
        if (auto* tray = w.findChild<DesktopTray*>())
            tray->restoreWindow();
    });
    w.show();
    if (app.arguments().contains("--extend"))
        QTimer::singleShot(300, &w, [&w] {
            w.findChild<QCheckBox*>("receive")->setChecked(false);
            w.findChild<QCheckBox*>("share")->setChecked(true);
            w.findChild<QCheckBox*>("extend")->setChecked(true);
            w.findChild<QPushButton*>("start")->click();
        });
    if (app.arguments().contains("--receive"))
        QTimer::singleShot(300, &w, [&w] {
            w.findChild<QCheckBox*>("share")->setChecked(false);
            w.findChild<QCheckBox*>("receive")->setChecked(true);
            w.findChild<QPushButton*>("start")->click();
        });
    return app.exec();
}
