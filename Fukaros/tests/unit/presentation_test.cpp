#include <QtTest>
#include <QVBoxLayout>
#include <windows.h>
#include "interface_support.h"

class PresentationTest : public QObject {
    Q_OBJECT
  private slots:
    void restoresNativeTitleBar_data() {
        QTest::addColumn<bool>("maximized");
        QTest::addColumn<int>("interruption");
        for (bool maximized : {false, true}) {
            for (int interruption = 0; interruption < 3; ++interruption) {
                const auto name = QByteArray(maximized ? "maximized-" : "normal-") +
                                  QByteArray::number(interruption);
                QTest::newRow(name.constData()) << maximized << interruption;
            }
        }
    }

    void restoresNativeTitleBar() {
        QFETCH(bool, maximized);
        QFETCH(int, interruption);
        QMainWindow window;
        auto* central = new QWidget(&window);
        auto* layout = new QVBoxLayout(central);
        auto* sidebar = new QWidget(central);
        auto* controls = new QWidget(central);
        auto* view = new ScreenView(central);
        layout->addWidget(sidebar);
        layout->addWidget(view, 1);
        layout->addWidget(controls);
        window.setCentralWidget(central);
        window.resize(800, 600);
        Presentation presentation(&window, sidebar, controls, view);
        QSignalSpy changes(&presentation, &Presentation::changed);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        if (maximized) {
            window.showMaximized();
            QTRY_VERIFY(window.isMaximized());
        }
        const auto flags = window.windowFlags();
        const auto geometry = window.geometry();
        constexpr LONG_PTR frameMask = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                                       WS_MAXIMIZEBOX | WS_THICKFRAME;
        const auto nativeFrame = [&] {
            return GetWindowLongPtrW(reinterpret_cast<HWND>(window.winId()), GWL_STYLE) & frameMask;
        };
        QCOMPARE(nativeFrame(), frameMask);
        for (int cycle = 0; cycle < 2; ++cycle) {
            presentation.setActive(true);
            QTRY_VERIFY(window.isFullScreen());
            QCOMPARE(nativeFrame() & WS_CAPTION, LONG_PTR(0));
            if (interruption == 1) {
                window.showMinimized();
                QTRY_VERIFY(window.isMinimized());
                window.setWindowState(window.windowState() & ~Qt::WindowMinimized);
                window.show();
            } else if (interruption == 2) {
                window.hide();
                window.show();
            }
            QTRY_VERIFY(window.isVisible() && window.isFullScreen());
            if (cycle == 0)
                QTest::keyClick(view, Qt::Key_Escape);
            else
                window.findChild<QPushButton*>("exitFullScreen")->click();
            QTRY_VERIFY(!window.isFullScreen());
            QCOMPARE(window.windowFlags(), flags);
            QCOMPARE(nativeFrame(), frameMask);
            QCOMPARE(window.isMaximized(), maximized);
            if (!maximized)
                QCOMPARE(window.geometry(), geometry);
            QVERIFY(sidebar->isVisible() && controls->isVisible());
            QVERIFY(!presentation.active());
            QCOMPARE(changes.count(), (cycle + 1) * 2);
        }
    }
};

QTEST_MAIN(PresentationTest)
#include "presentation_test.moc"
