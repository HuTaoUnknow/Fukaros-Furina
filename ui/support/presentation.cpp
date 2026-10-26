#include "interface_support.h"
#include <QEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#include <QLibrary>

namespace {
// Windows 11 may retain DWM's rounded/transparent frame on a Qt fullscreen
// window. Keep this local to presentation; normal windows retain their style.
struct FullscreenDwm {
    QLibrary library;
    using Set = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    using Get = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);
    Set set = nullptr;
    Get get = nullptr;

    FullscreenDwm() {
        wchar_t system[MAX_PATH]{};
        if (!GetSystemDirectoryW(system, MAX_PATH))
            return;
        library.setFileName(QString::fromWCharArray(system) + "/dwmapi.dll");
        set = reinterpret_cast<Set>(library.resolve("DwmSetWindowAttribute"));
        get = reinterpret_cast<Get>(library.resolve("DwmGetWindowAttribute"));
    }
};

FullscreenDwm& fullscreenDwm() {
    static FullscreenDwm api;
    return api;
}

constexpr DWORD cornerAttribute = 33, borderAttribute = 34;

void presentationFrame(QWidget* window, quint32 border, quint32 corner) {
    auto& api = fullscreenDwm();
    if (api.set) {
        const auto handle = reinterpret_cast<HWND>(window->winId());
        api.set(handle, borderAttribute, &border, sizeof(border));
        api.set(handle, cornerAttribute, &corner, sizeof(corner));
    }
}
} // namespace
#endif
Presentation::Presentation(QMainWindow* window, QWidget* sidebar, QWidget* controls, ScreenView* view,
                           ExitControl exitControl)
    : QObject(window), m_window(window), m_sidebar(sidebar), m_controls(controls), m_view(view) {
    if (exitControl == ExitControl::Button) {
        m_exit = new QPushButton("退出全屏", view);
        m_exit->setObjectName("exitFullScreen");
        m_exit->setMinimumHeight(36);
        m_exit->hide();
        connect(m_exit, &QPushButton::clicked, this, [this] { setActive(false); });
    }
    connect(view, &ScreenView::leaveFullScreen, this, [this] { setActive(false); });
    view->installEventFilter(this);
}

bool Presentation::eventFilter(QObject* watched, QEvent* event) {
    if (m_exit && watched == m_view && event->type() == QEvent::Resize) {
        m_exit->adjustSize();
        m_exit->move(qMax(4, m_view->width() - m_exit->width() -
                                 (m_view->findChild<QWidget*>("windowControls")
                                      ? m_view->findChild<QWidget*>("windowControls")->width() + 24
                                      : 12)),
                     12);
        m_exit->raise();
    }
    return QObject::eventFilter(watched, event);
}

void Presentation::setActive(bool on) {
    if (m_active == on)
        return;
    m_active = on;
    if (on) {
        m_geometry = m_window->geometry();
        m_wasMaximized = m_window->isMaximized();
        m_sidebar->hide();
        m_controls->hide();
#ifdef Q_OS_WIN
        m_windowFlags = m_window->windowFlags();
        auto& api = fullscreenDwm();
        const auto handle = reinterpret_cast<HWND>(m_window->winId());
        m_borderColor = 0xffffffffu;
        m_cornerPreference = 0;
        if (api.get) {
            api.get(handle, borderAttribute, &m_borderColor, sizeof(m_borderColor));
            api.get(handle, cornerAttribute, &m_cornerPreference, sizeof(m_cornerPreference));
        }
        auto* screen = m_window->screen();
        m_window->setWindowFlags(m_windowFlags | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        m_window->setScreen(screen);
#endif
        m_window->showFullScreen();
#ifdef Q_OS_WIN
        presentationFrame(m_window, 0xfffffffeu, 1); // DWMWA_COLOR_NONE / DWMWCP_DONOTROUND.
#endif
        if (m_exit) {
            m_exit->show();
            m_exit->raise();
        }
        m_view->setFocus();
    } else {
        if (m_exit)
            m_exit->hide();
        m_sidebar->show();
        m_controls->show();
#ifdef Q_OS_WIN
        // Qt restores the native style saved on entry when leaving fullscreen.
        // Clear that state before restoring our flags, otherwise its saved
        // frameless style overwrites the caption and resize frame again.
        m_window->hide();
        m_window->setWindowState(Qt::WindowNoState);
        m_window->setWindowFlags(m_windowFlags);
#endif
        if (m_wasMaximized)
            m_window->showMaximized();
        else {
            m_window->showNormal();
            m_window->setGeometry(m_geometry);
        }
#ifdef Q_OS_WIN
        presentationFrame(m_window, m_borderColor, m_cornerPreference);
#endif
    }
    emit changed(on);
}
