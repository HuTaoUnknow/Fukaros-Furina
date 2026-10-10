#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
class VirtualDriverTest:public QObject {
 Q_OBJECT
 int verify(const QString &package){
  QProcess process;process.start(QCoreApplication::applicationDirPath()+"/virtual_display_helper.exe",{"verify",QDir::toNativeSeparators(package)});
  if(!process.waitForFinished(15000))return -1;
  return process.exitStatus()==QProcess::NormalExit?process.exitCode():-1;
 }
private slots:
 void signedPackage(){QCOMPARE(verify(QCoreApplication::applicationDirPath()+"/virtual-display"),0);}
 void modifiedPackageRejected(){
  QTemporaryDir temporary;QVERIFY(temporary.isValid());
  const QString source=QCoreApplication::applicationDirPath()+"/virtual-display/";
  for(const QString &name:{QString("MttVDD.inf"),QString("MttVDD.dll"),QString("mttvdd.cat")})QVERIFY(QFile::copy(source+name,temporary.filePath(name)));
  QFile changed(temporary.filePath("MttVDD.inf"));QVERIFY(changed.open(QIODevice::Append));QVERIFY(changed.write("\n; altered package\n")>0);changed.close();
  QCOMPARE(verify(temporary.path()),11);
 }
};
QTEST_GUILESS_MAIN(VirtualDriverTest)
#include "virtual_driver_test.moc"
