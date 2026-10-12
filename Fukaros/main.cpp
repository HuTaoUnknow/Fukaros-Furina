#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>
#include "mainwindow.h"
#include "core/settings_identity.h"
int main(int argc,char **argv){
 QApplication app(argc,argv);QCoreApplication::setOrganizationName("Furina");QCoreApplication::setApplicationName("Fukaros");QCoreApplication::setApplicationVersion("0.4.10");furinaMigrateSettings("white_furina");MainWindow w;w.show();
 if(app.arguments().contains("--extend"))QTimer::singleShot(300,&w,[&w]{w.findChild<QCheckBox*>("receive")->setChecked(false);w.findChild<QCheckBox*>("share")->setChecked(true);w.findChild<QCheckBox*>("extend")->setChecked(true);w.findChild<QPushButton*>("start")->click();});
 if(app.arguments().contains("--receive"))QTimer::singleShot(300,&w,[&w]{w.findChild<QCheckBox*>("share")->setChecked(false);w.findChild<QCheckBox*>("receive")->setChecked(true);w.findChild<QPushButton*>("start")->click();});
 return app.exec();
}
