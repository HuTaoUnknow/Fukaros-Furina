#include "single_instance.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QThread>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

SingleInstance::SingleInstance(const QString &directory,QObject *parent):QObject(parent),m_directory(QDir(directory).absolutePath()){
 m_endpoint="Fukaros-"+QString::fromLatin1(QCryptographicHash::hash(m_directory.toUtf8(),QCryptographicHash::Sha256).toHex().left(32));
 m_server.setSocketOptions(QLocalServer::UserAccessOption);
 connect(&m_server,&QLocalServer::newConnection,this,[this]{
  while(m_server.hasPendingConnections()){
   auto *socket=m_server.nextPendingConnection();socket->setReadBufferSize(32);
   socket->write(QByteArray::number(QCoreApplication::applicationPid())+'\n');
   connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
   auto request=[this,socket]{
    if(!socket->canReadLine())return;
    if(socket->readLine(32)=="activate\n"){
     emit activateRequested();socket->write("ok\n");socket->flush();
    }
    socket->disconnectFromServer();
   };
   connect(socket,&QLocalSocket::readyRead,socket,request);
   QTimer::singleShot(2000,socket,[socket]{socket->abort();socket->deleteLater();});
   request();
  }
 });
}
bool SingleInstance::activateOwner(){
 QLocalSocket socket;socket.connectToServer(m_endpoint);
 if(!socket.waitForConnected(100))return false;
 if(!socket.canReadLine()&&!socket.waitForReadyRead(500))return false;
 bool valid=false;const auto pid=socket.readLine(32).trimmed().toUInt(&valid);if(!valid||!pid)return false;
#ifdef Q_OS_WIN
 // A shortcut launch may grant its foreground right to the existing process.
 AllowSetForegroundWindow(pid);
#endif
 socket.write("activate\n");if(!socket.waitForBytesWritten(300))return false;
 if(!socket.canReadLine()&&!socket.waitForReadyRead(500))return false;
 return socket.readLine(32)=="ok\n";
}
SingleInstance::Result SingleInstance::start(){
 if(!QDir().mkpath(m_directory)){m_error=tr("Cannot create the application lock directory.");return Error;}
 m_lock=std::make_unique<QLockFile>(m_directory+"/instance.lock");m_lock->setStaleLockTime(0);
 QElapsedTimer elapsed;elapsed.start();
 do{
  if(m_lock->tryLock()){
   // Only the lock owner may remove a stale endpoint left by a crash.
   QLocalServer::removeServer(m_endpoint);
   if(m_server.listen(m_endpoint))return Primary;
   m_error=m_server.errorString();m_lock->unlock();return Error;
  }
  if(m_lock->error()!=QLockFile::LockFailedError){m_error=tr("Cannot lock the application instance.");return Error;}
  if(activateOwner())return Activated;
  QThread::msleep(50);
 }while(elapsed.elapsed()<3000);
 m_error=tr("The existing application did not respond. Please try again.");return Error;
}
