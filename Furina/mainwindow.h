#pragma once
#include <QMainWindow>
#include <QTimer>
#include <QDialog>
#include "peer.h"
#include "screenview.h"
#include "interface_support.h"
#include "session_consent.h"
#include "cast_ui_state.h"
#include "androidbridge.h"
#include "desktop_video.h"
#include "video_policy.h"
namespace Ui{class MainWindow;class SettingsDialog;}
class MainWindow:public QMainWindow {
 Q_OBJECT
public:
 explicit MainWindow(QWidget *parent=nullptr);
 ~MainWindow() override;
protected:
 void resizeEvent(QResizeEvent *event) override;
private:
 Ui::MainWindow *ui;Ui::SettingsDialog *cfg;QDialog *m_settings;DirectionChoice *m_direction;SessionConsent *m_consent;CastUiState *m_castUi;
 Peer m_peer;AndroidBridge m_android;ScreenView *m_view;Presentation *m_presentation;QTimer m_tick;
 DesktopVideoReceiver m_videoReceiver;ResolutionChoice *m_resolution;fr_video_policy m_videoPolicy{};
 double m_remoteFps=60,m_remoteWorkMs=0,m_encodeWorkMs=0,m_decodeWorkMs=0;unsigned m_remoteQueue=0;bool m_videoVisible=false;QRect m_nativeRect;bool m_nativeVisible=false,m_nativeFull=false;
 bool m_sharing=false,m_listening=false,m_pending=false,m_sessionActive=false,m_remoteMax=false;
 void log(const QString &message,const QString &english={});void loadSettings();void saveSettings();void openSettings();
 void start();void stop();void startCapture();void stopCapture();void applyDirection();void directionChanged();void requestDirection();void endSession();void session(quint32 action);void maximize();
 void updateVideoGeometry();
};
