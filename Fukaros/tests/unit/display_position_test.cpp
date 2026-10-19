#include <QtTest>
#include <QSettings>
#include <QDialog>
#include <QPushButton>
#include <QStackedWidget>
#include <QLabel>
#include "display_position.h"
#include "interface_support.h"

class DisplayPositionTest : public QObject {
    Q_OBJECT
  private slots:

    void initTestCase() {
        QCoreApplication::setOrganizationName("FurinaTests");
        QCoreApplication::setApplicationName("Position049");
    }

    void init() {
        QSettings settings;
        settings.remove("extensionPosition");
        settings.sync();
    }

    void firstConnectionSavesPositionAndNextConnectionRestoresIt() {
        QWidget host;
        host.setStyleSheet(furinaStyle());
        host.show();
        QPoint current(-1920, 0);
        int opened = 0, applied = 0;
        {
            DisplayPosition flow(
                &host,
                [&](QPoint* point) {
                    *point = current;
                    return true;
                },
                [&](const QPoint& point) {
                    current = point;
                    ++applied;
                    return true;
                },
                [&] {
                    ++opened;
                    return true;
                });
            QSignalSpy ready(&flow, &DisplayPosition::ready);
            flow.begin();
            auto* dialog = host.findChild<QDialog*>("PositionDialog");
            QVERIFY(dialog->isVisible());
            QCOMPARE(opened, 1);
            QCOMPARE(applied, 0);
            QCOMPARE(ready.count(), 0);
            dialog->findChild<QPushButton*>("finishPosition")->click();
            QCOMPARE(ready.count(), 1);
            QVERIFY(flow.configured() && !flow.active());
            QVERIFY(!dialog->isVisible());
        }
        current = QPoint(3840, 0);
        DisplayPosition next(
            &host,
            [&](QPoint* point) {
                *point = current;
                return true;
            },
            [&](const QPoint& point) {
                current = point;
                ++applied;
                return true;
            },
            [&] {
                ++opened;
                return true;
            });
        QSignalSpy ready(&next, &DisplayPosition::ready);
        next.begin();
        auto* dialog = host.findChild<QDialog*>("PositionDialog");
        QCOMPARE(dialog->findChild<QStackedWidget*>("positionPages")->currentWidget()->objectName(),
                 QString("choicePage"));
        QCOMPARE(opened, 1);
        dialog->grab().save("work/position-choice-049.png");
        dialog->findChild<QPushButton*>("inheritPosition")->click();
        QCOMPARE(current, QPoint(-1920, 0));
        QCOMPARE(applied, 1);
        QCOMPARE(ready.count(), 1);
        QCOMPARE(opened, 1);
        // A later layout adjustment is remembered on stop, in desktop pixels.
        current = QPoint(0, -1200);
        next.rememberCurrent();
        QSettings settings;
        QCOMPARE(settings.value("extensionPosition/y").toInt(), -1200);
    }

    void reconfigureAndCancelPreserveLastConfirmedPosition() {
        QSettings settings;
        settings.setValue("extensionPosition/valid", true);
        settings.setValue("extensionPosition/x", -1920);
        settings.setValue("extensionPosition/y", 0);
        settings.sync();
        QWidget host;
        host.setStyleSheet(furinaStyle());
        host.show();
        QPoint current(0, -1200);
        int opened = 0;
        DisplayPosition flow(
            &host,
            [&](QPoint* point) {
                *point = current;
                return true;
            },
            [](const QPoint&) { return true; },
            [&] {
                ++opened;
                return true;
            });
        QSignalSpy ready(&flow, &DisplayPosition::ready), cancelled(&flow, &DisplayPosition::cancelled);
        flow.begin();
        auto* dialog = host.findChild<QDialog*>("PositionDialog");
        dialog->findChild<QPushButton*>("reconfigurePosition")->click();
        QCOMPARE(opened, 1);
        QCOMPARE(ready.count(), 0);
        dialog->grab().save("work/position-configure-049.png");
        dialog->findChild<QPushButton*>("cancelPosition")->click();
        QCOMPARE(cancelled.count(), 1);
        QVERIFY(!flow.configured());
        settings.sync();
        QCOMPARE(settings.value("extensionPosition/x").toInt(), -1920);
        flow.begin();
        dialog->findChild<QPushButton*>("reconfigurePosition")->click();
        dialog->findChild<QPushButton*>("finishPosition")->click();
        settings.sync();
        QCOMPARE(settings.value("extensionPosition/y").toInt(), -1200);
        QCOMPARE(ready.count(), 1);
    }

    void changedMonitorLayoutFallsBackToConfigurationAndResetCancelsStaleButtons() {
        QSettings settings;
        settings.setValue("extensionPosition/valid", true);
        settings.setValue("extensionPosition/x", 3840);
        settings.setValue("extensionPosition/y", 0);
        settings.sync();
        QWidget host;
        host.show();
        int opened = 0;
        bool readable = false;
        DisplayPosition flow(
            &host,
            [&](QPoint* point) {
                *point = QPoint(-1920, 0);
                return readable;
            },
            [](const QPoint&) { return false; },
            [&] {
                ++opened;
                return false;
            });
        QSignalSpy ready(&flow, &DisplayPosition::ready), cancelled(&flow, &DisplayPosition::cancelled);
        flow.begin();
        auto* dialog = host.findChild<QDialog*>("PositionDialog");
        dialog->findChild<QPushButton*>("inheritPosition")->click();
        QCOMPARE(opened, 1);
        QVERIFY(dialog->findChild<QLabel*>("positionError")->isVisible());
        dialog->findChild<QPushButton*>("finishPosition")->click();
        QCOMPARE(ready.count(), 0);
        QVERIFY(flow.active());
        settings.sync();
        QCOMPARE(settings.value("extensionPosition/x").toInt(), 3840);
        readable = true;
        flow.reset();
        dialog->findChild<QPushButton*>("finishPosition")->click();
        QCOMPARE(ready.count(), 0);
        QCOMPARE(cancelled.count(), 0);
        QVERIFY(!flow.configured());
    }
};
QTEST_MAIN(DisplayPositionTest)
#include "display_position_test.moc"
