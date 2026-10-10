#include "adb.h"
Adb::Adb(QObject *p):QObject(p){
 m_timeout.setSingleShot(true);m_timeout.setInterval(12000);
 connect(&m_timeout,&QTimer::timeout,this,[this]{emit status(tr("ADB timed out"));m_process.kill();});
 connect(&m_process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus s){finish(code==0&&s==QProcess::NormalExit);});
 connect(&m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart){emit status(m_process.errorString());finish(false);}});
}
Adb::~Adb(){cancel();}
void Adb::cancel(){m_jobs.clear();m_running=false;m_active.done={};m_timeout.stop();m_process.kill();m_process.waitForFinished(1500);}
void Adb::run(const QStringList &args,std::function<void(bool,QByteArray)> done,bool device){
 if(device&&serial.trimmed().isEmpty()){emit status(tr("Select a device serial first"));if(done)done(false,{});return;}
 if(m_jobs.size()>=16){emit status(tr("ADB queue is full"));if(done)done(false,{});return;}
 QStringList a;if(device)a<<"-s"<<serial.trimmed();a+=args;m_jobs.enqueue({a,done});next();
}
void Adb::next(){if(m_running||m_jobs.isEmpty())return;m_running=true;m_active=m_jobs.dequeue();m_process.setProgram(program);m_process.setArguments(m_active.args);m_process.start();m_timeout.start();}
void Adb::finish(bool ok){
 if(!m_running)return;m_timeout.stop();m_running=false;const auto out=m_process.readAllStandardOutput();const auto err=m_process.readAllStandardError();auto cb=std::move(m_active.done);
 if(!ok)emit status(QString::fromUtf8(err).trimmed().isEmpty()?tr("ADB command failed"):QString::fromUtf8(err).trimmed());
 if(cb)cb(ok,out);next();
}
