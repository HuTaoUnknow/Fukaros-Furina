#pragma once
#include <QMainWindow>
#include <QTimer>
#include <QProcess>
#include <QPointer>
#include <QScreen>
#include <QDialog>
#include "cast_source.h"
#include "peer.h"
#include "screenview.h"
#include "interface_support.h"
#include "session_consent.h"
#include "cast_ui_state.h"
#include "cast_control.h"
#include "adb.h"
#include "usb_network.h"
#include "desktop_video.h"
#include "virtual_display_controller.h"
#include "display_position.h"
namespace Ui {class MainWindow;class SettingsDialog;}
class MainWindow : public QMainWindow {
 Q_OBJECT
public:
 explicit MainWindow(QWidget *parent=nullptr);
 ~MainWindow() override;
protected:
 void resizeEvent(QResizeEvent *event) override;
private:
 Ui::MainWindow *ui;Ui::SettingsDialog *cfg;QDialog *m_settings;DirectionChoice *m_direction;SessionConsent *m_consent;CastUiState *m_castUi;CastControl *m_castControl;
 Peer m_peer;Adb m_adb;UsbNetwork m_usbNetwork;ScreenView *m_view;Presentation *m_presentation;
 VirtualDisplayController m_virtual;
 DisplayPosition *m_position;
 DesktopVideoSender m_videoSender;DesktopVideoReceiver m_videoReceiver;ResolutionChoice *m_resolution;
 QElapsedTimer m_displaySample;double m_encodeWorkMs=0;
 QTimer m_capture,m_preview,m_screenshotTimeout,m_layoutRefresh;QProcess m_screenshot;QSize m_deviceSize;
 QPointer<QScreen> m_captureScreen;
 QString m_captureName;
 bool m_adbOnly=false,m_sharing=false,m_pending=false,m_sessionActive=false,m_remoteMax=false;
 int m_generation=0;
 void log(const QString &message,const QString &english={});
 void loadSettings();void saveSettings();void openSettings();void configureAdb();
 void start();void stop();void connectUsb(int generation);void applyDirection();void directionChanged();void requestDirection();void endSession();
 void preview();void gesture(fr_gesture gesture);void key(quint32 key);void session(quint32 action);
 fr_cast_mode castMode() const;bool usableScreen(QScreen *screen) const;QScreen *selectedScreen() const;
 void observeScreen(QScreen *screen);void refreshScreens();bool startSharing();void stopSharing();void maximize();
 void displayChanged();void rebindCapture();
};
