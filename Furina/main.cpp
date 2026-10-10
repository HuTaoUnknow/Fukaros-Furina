#include <QApplication>
#include "mainwindow.h"
#include "core/settings_identity.h"
int main(int argc,char **argv){QApplication app(argc,argv);QCoreApplication::setOrganizationName("Furina");QCoreApplication::setApplicationName("Furina");QCoreApplication::setApplicationVersion("0.4.1");furinaMigrateSettings("black_furina");MainWindow w;w.show();return app.exec();}
