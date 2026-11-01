#include <QApplication>
#include "mainwindow.h"
#include "settings_identity.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Furina");
    QCoreApplication::setApplicationName("Furina");
    QCoreApplication::setApplicationVersion("0.4.20_ZT");
    QApplication::setApplicationDisplayName("Furina_ZT");
    furinaMigrateSettings("black_furina");
    MainWindow w;
    w.show();
    return app.exec();
}
