#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QDateTime>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <cstdio>
#include "mainwindow.h"

// Deliberate USB/device test using the actual window, consent, DPI controller,
// capture and encoder. Run with adb forward tcp:28183 tcp:27183 first.
class Pattern : public QWidget {
  public:
    QElapsedTimer clock;
    Pattern() {
        setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
        clock.start();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.scale(width() / 1920.0, height() / 1200.0);
        p.fillRect(0, 0, 1920, 1200, QColor(24, 34, 48));
        p.setPen(QColor(180, 210, 230));
        p.setFont(QFont("Arial", 18));
        for (int y = 40; y < 1200; y += 50)
            p.drawText(30, y, "Furina scale A/B: same animated scene, 1920 x 1200, target 60 FPS");
        p.fillRect((clock.elapsed() % 4000) * 1820 / 4000, 400, 100, 300, QColor(50, 180, 225));
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setOrganizationName("FurinaHardware");
    app.setApplicationName("ScaleStreamAB");
    QSettings original("Furina", "Fukaros"), settings;
    const auto token = original.value("token").toString();
    if (token.isEmpty())
        return 77;
    settings.clear();
    for (const char* key : {"extensionPosition/x", "extensionPosition/y", "extensionPosition/valid"})
        settings.setValue(key, original.value(key));
    if (!settings.value("extensionPosition/valid").toBool())
        return 77;
    settings.setValue("screenScalePercent", original.value("screenScalePercent", 150));
    settings.setValue("screenScaleEnabled", original.value("screenScaleEnabled", true));
    settings.setValue("screenScaleRevision", original.value("screenScaleRevision", 0));
    MainWindow window;
    window.show();
    auto* peer = window.findChild<Peer*>();
    auto* toggle = window.findChild<QCheckBox*>("screenScaleEnabled");
    auto* scale = window.findChild<QComboBox*>("screenScale");
    auto* consent = window.findChild<SessionConsent*>();
    window.findChild<QCheckBox*>("share")->setChecked(true);
    window.findChild<QCheckBox*>("extend")->setChecked(true);
    window.findChild<ResolutionChoice*>()->setValue(1920);
    Pattern pattern;
    QElapsedTimer clock, phaseClock;
    QTimer tick, phaseTimer;
    int phase = -1, samples = 0;
    bool done = false, running = false, savedPeer = false, savedEnabled = true;
    int savedPercent = 150;
    quint64 savedRevision = 0;
    auto finish = [&](int code) {
        if (done)
            return;
        done = true;
        tick.stop();
        phaseTimer.stop();
        pattern.hide();
        if (savedPeer)
            peer->sendScreenScale(savedPercent, qMax(savedRevision + 1, quint64(QDateTime::currentMSecsSinceEpoch()) + 100), savedEnabled);
        QTimer::singleShot(150, &app, [&, code] {
            window.findChild<QPushButton*>("start")->click();
            const auto log = window.findChild<QPlainTextEdit*>("log")->toPlainText().toUtf8();
            printf("APP_LOG_BEGIN\n%s\nAPP_LOG_END\nRESULT exit=%d samples=%d\n", log.constData(), code, samples);
            fflush(stdout);
            app.exit(code);
        });
    };
    QObject::connect(peer, &Peer::screenScalePreference, &app, [&](int p, quint64 r, bool e) {
        if (!savedPeer) {
            savedPeer = true;
            savedPercent = p;
            savedRevision = r;
            savedEnabled = e;
        }
    });
    QObject::connect(consent, &SessionConsent::incoming, &app, [&] {
        QTimer::singleShot(100, &app, [&] { window.findChild<QPushButton*>("consentAccept")->click(); });
    });
    auto nextPhase = [&] {
        if (++phase >= 4) {
            finish(0);
            return;
        }
        scale->setCurrentIndex(scale->findData(150));
        toggle->setChecked((phase % 2) != 0);
        phaseClock.restart();
        printf("PHASE index=%d scale=%d\n", phase, toggle->isChecked() ? 150 : 100);
        fflush(stdout);
    };
    QObject::connect(peer, &Peer::feedback, &app, [&](double fps, double work, unsigned queue, double shown) {
        if (phase < 0) {
            nextPhase();
            phaseTimer.start(20000);
        }
        printf("FEEDBACK phase=%d ms=%lld decoded=%.3f shown=%.3f work=%.3f queue=%u\n",
               phase, phaseClock.isValid() ? phaseClock.elapsed() : 0, fps, shown, work, queue);
        ++samples;
        fflush(stdout);
    });
    QObject::connect(peer, &Peer::status, &app, [](const QString& s) {
        printf("PEER %s\n", s.toUtf8().constData());
        fflush(stdout);
    });
    QObject::connect(&phaseTimer, &QTimer::timeout, &app, nextPhase);
    QObject::connect(&tick, &QTimer::timeout, &app, [&] {
        if (auto* inherit = window.findChild<QPushButton*>("inheritPosition"); inherit && inherit->isVisible())
            inherit->click();
        if (auto* complete = window.findChild<QPushButton*>("finishPosition"); complete && complete->isVisible())
            complete->click();
        if (running) {
            for (auto* screen : app.screens())
                if (screen != app.primaryScreen() && pattern.geometry() != screen->geometry()) {
                    pattern.setGeometry(screen->geometry());
                    break;
                }
            pattern.update();
            return;
        }
        if (!consent->active())
            return;
        for (auto* screen : app.screens())
            if (screen != app.primaryScreen()) {
                pattern.setGeometry(screen->geometry());
                pattern.show();
                running = true;
                clock.start();
                break;
            }
    });
    QObject::connect(peer, &Peer::disconnected, &app, [&] { if (!done) finish(2); });
    tick.start(16);
    QTimer::singleShot(150000, &app, [&] { finish(3); });
    peer->connectTo("127.0.0.1", 28183, token);
    return app.exec();
}
