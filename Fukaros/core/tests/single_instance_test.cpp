#include "single_instance.h"
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

static void marker(const QString &path,const QByteArray &data){QFile f(path);if(f.open(QIODevice::WriteOnly|QIODevice::Append))f.write(data);}
class InstanceTest:public QObject {
 Q_OBJECT
private slots:
 void relaunchAndCrashRecovery(){
  QTemporaryDir directory;QVERIFY(directory.isValid());
  const QString base=directory.path(),ready=base+"/ready",activated=base+"/activated";
  const QString exe=QCoreApplication::applicationFilePath();QProcess owner;
  owner.start(exe,{"--owner",base});QVERIFY(owner.waitForStarted());QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(ready),4000);
  QProcess second;second.start(exe,{"--secondary",base});QVERIFY(second.waitForFinished(5000));QCOMPARE(second.exitCode(),0);
  QTRY_VERIFY(QFile::exists(activated));QCOMPARE(owner.state(),QProcess::Running);
  // A different install directory still uses this same per-user lock path.
  QProcess third;third.start(exe,{"--secondary",base});QVERIFY(third.waitForFinished(5000));QCOMPARE(third.exitCode(),0);
  owner.kill();QVERIFY(owner.waitForFinished());QFile::remove(ready);
  QProcess replacement;replacement.start(exe,{"--owner",base});QVERIFY(replacement.waitForStarted());QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(ready),4000);
  QProcess afterCrash;afterCrash.start(exe,{"--secondary",base});QVERIFY(afterCrash.waitForFinished(5000));QCOMPARE(afterCrash.exitCode(),0);
  replacement.kill();QVERIFY(replacement.waitForFinished());
 }
};
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);const auto args=app.arguments();
 if(args.size()==3){SingleInstance instance(args[2]);const auto result=instance.start();
  if(args[1]=="--secondary")return result==SingleInstance::Activated?0:2;
  if(args[1]=="--owner"){
   if(result!=SingleInstance::Primary)return 3;
   QObject::connect(&instance,&SingleInstance::activateRequested,&app,[&]{marker(args[2]+"/activated","activate\n");});
   marker(args[2]+"/ready","ready");QTimer::singleShot(15000,&app,&QCoreApplication::quit);return app.exec();
  }
 }
 InstanceTest test;return QTest::qExec(&test,argc,argv);
}
#include "single_instance_test.moc"
