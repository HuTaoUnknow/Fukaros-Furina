#pragma once
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QTimer>
#include <functional>

class Adb : public QObject {
    Q_OBJECT
  public:
    explicit Adb(QObject* parent = nullptr);
    ~Adb() override;
    QString program, serial;
    void run(const QStringList& args, std::function<void(bool, QByteArray)> done = {}, bool device = true);
    void cancel();
  signals:
    void status(const QString& message);

  private:
    struct Job {
        QStringList args;
        std::function<void(bool, QByteArray)> done;
    };

    QQueue<Job> m_jobs;
    QProcess m_process;
    QTimer m_timeout;
    Job m_active;
    bool m_running = false;
    void next();
    void finish(bool ok);
};
