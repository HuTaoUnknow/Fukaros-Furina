#include <QtTest>
#include <QAction>
#include <QSystemTrayIcon>
#include <QWidget>
#include "desktop_tray.h"

class DesktopTrayTest:public QObject {
 Q_OBJECT
private slots:
 void closeRestoreAndExplicitExit(){
  if(!QSystemTrayIcon::isSystemTrayAvailable())QSKIP("A Windows notification area is required");
  QWidget window;DesktopTray tray(&window,QIcon(":/icons/app.png"));window.show();
  QSignalSpy hidden(&tray,&DesktopTray::hiddenToTray),quit(&tray,&DesktopTray::quitRequested);
  QVERIFY(tray.available());QVERIFY(!window.close());QVERIFY(!window.isVisible());QCOMPARE(hidden.size(),1);QVERIFY(tray.available());
  auto *show=window.findChild<QAction*>("trayShow"),*exit=window.findChild<QAction*>("trayQuit");QVERIFY(show&&exit);
  show->trigger();QTRY_VERIFY(window.isVisible());QVERIFY(!window.isMinimized());
  window.showMaximized();QTRY_VERIFY(window.isMaximized());QVERIFY(!window.close());show->trigger();QTRY_VERIFY(window.isVisible()&&window.isMaximized());
  window.showMinimized();tray.restoreWindow();QTRY_VERIFY(!window.isMinimized()&&window.isVisible());
  QMetaObject::invokeMethod(window.findChild<QSystemTrayIcon*>(),"activated",Qt::DirectConnection,Q_ARG(QSystemTrayIcon::ActivationReason,QSystemTrayIcon::Trigger));QVERIFY(window.isVisible());
  exit->trigger();QCOMPARE(quit.size(),1);QVERIFY(!tray.available());QVERIFY(window.close());QVERIFY(!window.isVisible());
 }
 void withoutTrayAllowsNormalClose(){
  QWidget window;DesktopTray tray(&window,QIcon());QSignalSpy hidden(&tray,&DesktopTray::hiddenToTray);
  window.show();QVERIFY(!tray.available());QVERIFY(window.close());QVERIFY(!window.isVisible());QCOMPARE(hidden.size(),0);
 }
};
QTEST_MAIN(DesktopTrayTest)
#include "desktop_tray_test.moc"
