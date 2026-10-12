#pragma once
#include <QObject>
class QMainWindow;
class QWidget;
class QBoxLayout;
class QCheckBox;
class QPushButton;

// Reuse the same controls and session state when the device rotates.
class MobileLayout final : public QObject {
public:
 explicit MobileLayout(QMainWindow *window);
 void refresh(bool fullscreen);
private:
 QMainWindow *m_window;
 QWidget *m_sidebar,*m_toolbar,*m_tabs,*m_consentHost,*m_consent,*m_bottom,*m_direction,*m_debug,*m_preview,*m_debugTitle;
 QBoxLayout *m_sideLayout,*m_toolLayout,*m_contentLayout,*m_bottomLayout,*m_debugLayout,*m_consentLayout;
 QCheckBox *m_modes,*m_logs;
 QPushButton *m_start,*m_settings,*m_maximize;
 bool m_portrait=false,m_initialized=false,m_arranging=false,m_fullscreen=false;
};
