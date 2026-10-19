#include <QtTest>
#include <QScrollArea>
#include <QScrollBar>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QEnterEvent>
#include <QScroller>
#include <QDialog>
#include "settings_support.h"

class SettingsSupportTest : public QObject {
    Q_OBJECT
  private slots:

    void longStepsStayInEmbeddedDialogAndClearTheirTrigger() {
        QWidget activity;
        activity.resize(640, 700);
        QDialog settings(&activity, Qt::Widget);
        settings.setGeometry(100, 120, 400, 420);
        auto* layout = new QVBoxLayout(&settings);
        layout->addStretch();
        auto* help = new StepHelpButton;
        layout->addWidget(help, 0, Qt::AlignRight);
        layout->addStretch();
        help->setHelpTitle("长步骤");
        help->setSteps(QString("详细操作步骤\n").repeated(35));
        activity.show();
        settings.show();
        QTest::qWait(30);
        QTest::mouseMove(help, help->rect().center());
        QTest::mouseClick(help, Qt::LeftButton);
        QTest::qWait(30);
        auto* panel = settings.findChild<QFrame*>("stepsPopup");
        QVERIFY(panel && panel->isVisible());
        QCOMPARE(panel->parentWidget(), &settings);
        QVERIFY(settings.rect().contains(panel->geometry()));
        QVERIFY(!panel->geometry().intersects(QRect(help->mapTo(&settings, QPoint()), help->size())));
        QVERIFY(panel->findChild<QScrollArea*>("stepsScroll")->verticalScrollBar()->maximum() > 0);
        settings.hide();
        QVERIFY(!panel->isVisible());
    }

    void hoverDoesNotOpenStepsAndClickTogglesThem() {
        QWidget host;
        host.resize(480, 360);
        auto* layout = new QVBoxLayout(&host);
        auto* help = new StepHelpButton;
        layout->addWidget(help, 0, Qt::AlignRight);
        layout->addStretch();
        help->setHelpTitle("USB 操作步骤");
        help->setSteps("1. 接好数据线。\n2. 打开 USB 网络共享。\n3. 保存并按开始。");
        host.show();
        QTest::qWait(30);
        const auto center = help->rect().center();
        QEnterEvent enter(center, help->mapTo(&host, center), help->mapToGlobal(center));
        QApplication::sendEvent(help, &enter);
        QVERIFY(!host.findChild<QFrame*>("stepsPopup"));
        help->click();
        auto* panel = host.findChild<QFrame*>("stepsPopup");
        QVERIFY(panel && panel->isVisible());
        QVERIFY(host.rect().contains(panel->geometry()));
        QVERIFY(panel->findChild<QLabel*>("stepsBody")->text().contains("2. 打开 USB 网络共享"));
        // Moving away leaves clicked instructions open until explicitly dismissed.
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(help, &leave);
        QTest::qWait(220);
        QVERIFY(panel->isVisible());
        help->click();
        QVERIFY(!panel->isVisible());
        QApplication::sendEvent(help, &enter);
        QVERIFY(!panel->isVisible());
        help->click();
        QVERIFY(panel->isVisible());
        QTest::mouseClick(&host, Qt::LeftButton, Qt::NoModifier, QPoint(15, 340));
        QVERIFY(!panel->isVisible());
        help->click();
        QVERIFY(panel->isVisible());
        panel->findChild<QPushButton*>("closeSteps")->click();
        QVERIFY(!panel->isVisible());
    }

    void fingerSwipeScrollsContentAndTapStillWorks() {
        QWidget host;
        host.resize(460, 350);
        auto* outer = new QVBoxLayout(&host);
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        outer->addWidget(scroll);
        auto* content = new QWidget;
        auto* layout = new QVBoxLayout(content);
        auto* label = new QLabel("在内容上滑动");
        label->setMinimumHeight(100);
        layout->addWidget(label);
        auto* help = new StepHelpButton;
        help->setHelpTitle("触控帮助");
        help->setSteps("1. 点击可以查看步骤。\n2. 上下滑动可以移动设置。");
        layout->addWidget(help);
        auto* edit = new QLineEdit("测试输入");
        layout->addWidget(edit);
        for (int i = 0; i < 16; ++i) {
            auto* row = new QLabel(QString("设置行 %1").arg(i));
            row->setMinimumHeight(55);
            layout->addWidget(row);
        }
        scroll->setWidget(content);
        furinaConfigureSettingsScroll(scroll);
        host.show();
        QTest::qWait(40);
        QVERIFY(scroll->verticalScrollBar()->maximum() > 200);
        auto* device = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
        // A short finger tap on the ellipsis must remain a click, not a scroll.
        QTest::touchEvent(help, device).press(0, help->rect().center(), help);
        QTest::qWait(40);
        QTest::touchEvent(help, device).release(0, help->rect().center(), help);
        auto* popup = host.findChild<QFrame*>("stepsPopup");
        QTRY_VERIFY(popup && popup->isVisible());
        popup->findChild<QPushButton*>("closeSteps")->click();
        auto* viewport = scroll->viewport();
        const QPoint from(viewport->width() / 2, viewport->height() - 35);
        QTest::touchEvent(viewport, device).press(0, from, viewport);
        QTest::qWait(30);
        for (int i = 1; i <= 6; ++i) {
            QTest::touchEvent(viewport, device).move(0, from - QPoint(0, i * 25), viewport);
            QTest::qWait(20);
        }
        QTest::touchEvent(viewport, device).release(0, from - QPoint(0, 150), viewport);
        QTRY_VERIFY(scroll->verticalScrollBar()->value() > 40);
        // A reverse finger swipe moves back, and taps never modify a field while
        // a drag is in progress.
        QScroller::scroller(viewport)->stop();
        const int before = scroll->verticalScrollBar()->value();
        const QPoint back(viewport->width() / 2, 45);
        QTest::touchEvent(viewport, device).press(0, back, viewport);
        QTest::qWait(30);
        for (int i = 1; i <= 5; ++i) {
            QTest::touchEvent(viewport, device).move(0, back + QPoint(0, i * 20), viewport);
            QTest::qWait(20);
        }
        QTest::touchEvent(viewport, device).release(0, back + QPoint(0, 100), viewport);
        QTRY_VERIFY(scroll->verticalScrollBar()->value() < before);
        QCOMPARE(edit->text(), QString("测试输入"));
        QScroller::scroller(viewport)->stop();
    }
};
QTEST_MAIN(SettingsSupportTest)
#include "settings_support_test.moc"
