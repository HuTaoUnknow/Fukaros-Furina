#pragma once
#include <QMainWindow>
#include <QTimer>
#include <QProcess>
#include <QPointer>
#include <QScreen>
#include <QDialog>
#include "cast_source.h"
#include "peer.h"
#include "pointer_settings.h"
#include "desktop_pointer.h"
#include "desktop_cursor_stream.h"
#include "screenview.h"
#include "interface_support.h"
#include "session_consent.h"
#include "presentation_sync.h"
#include "cast_ui_state.h"
#include "cast_control.h"
#include "adb.h"
#include "usb_network.h"
#include "lan_discovery.h"
#include "desktop_video.h"
#include "virtual_display_controller.h"
#include "display_position.h"

namespace Ui {
class MainWindow;
class SettingsDialog;
} // namespace Ui

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    void setupInterface();
    void connectSessionEvents();
    void connectVideoEvents();
    void setupInput();
    void connectActions();
    void connectTransportEvents();
    void connectDebugActions();
    void connectDisplayEvents();
    void setupCaptureTimers();
    Ui::MainWindow* ui;
    Ui::SettingsDialog* cfg;
    QDialog* m_settings;
    DirectionChoice* m_direction;
    SessionConsent* m_consent;
    PresentationSync* m_presentationSync;
    CastUiState* m_castUi;
    CastControl* m_castControl;
    Peer m_peer;
    Adb m_adb;
    UsbNetwork m_usbNetwork;
    LanDiscovery m_lanDiscovery;
    QTimer m_connectTimeout;
    QStringList m_lanCandidates;
    QString m_lanConnectHost;
    QString m_lanPairingSecret;
    // Pairing-code matching runs until the settings dialog closes. It keeps
    // re-probing while waiting and reconnects while paired.
    QTimer m_pairRetry;
    QStringList m_pairTargets;   // addresses that answered, in probe order
    QString m_pairCode;          // normalized code, empty when not matching
    QString m_pairPeerIp;        // last address of the other side
    QString m_pairPeerName;      // code the other side was matched with
    bool m_pairing = false;      // matching (not yet paired)
    bool m_pairPaired = false;   // paired during this matching session
    // One broadcast round per Start; see MainWindow::searchLan.
    bool m_lanSearched = false;
    ScreenView* m_view;
    Presentation* m_presentation;
    VirtualDisplayController m_virtual;
    DesktopPointer m_pointer;
    DesktopCursorStream* m_cursorStream = nullptr;
    bool m_independentCursor = true;
    void updateCursorMapping();
    DisplayPosition* m_position;
    DesktopVideoSender m_videoSender;
    DesktopVideoReceiver m_videoReceiver;
    ResolutionChoice* m_resolution;
    QElapsedTimer m_displaySample;
    double m_encodeWorkMs = 0;
    QTimer m_capture, m_preview, m_screenshotTimeout, m_layoutRefresh;
    QProcess m_screenshot;
    QSize m_deviceSize;
    QPointer<QScreen> m_captureScreen;
    QString m_captureName;
    bool m_adbOnly = false, m_sharing = false, m_pending = false, m_sessionActive = false,
         m_remoteMax = false, m_oneTimeListen = false, m_pairingOnly = false;
    QString m_oneTimeSecret;
    qint64 m_oneTimeExpiry = 0;
    QTimer m_qrExpiry;
    int m_generation = 0;
    void log(const QString& message, const QString& english = {});
    void loadSettings();
    void saveSettings();
    void openSettings();
    void bindPairingUi();
    void refreshPairingHint();
    void showPairingQr();
    void pairWithCode();
    void searchLan();
    void tryNextLanHost();
    void beginPairMatch();
    void endPairMatch();
    void startPairProbe();
    void pairProbeHosts(const QStringList& hosts);
    void pairConnectNext();
    void updatePairingHint();
    void clearPairingQr();
    void retireOneTimeListener();
    void configureAdb();
    void start();
    void stop();
    void connectUsb(int generation);
    void applyDirection();
    void directionChanged();
    void requestDirection();
    void endSession();
    void preview();
    void gesture(fr_gesture gesture);
    void key(quint32 key);
    void session(quint32 action);
    fr_cast_mode castMode() const;
    bool usableScreen(QScreen* screen) const;
    QScreen* selectedScreen() const;
    void observeScreen(QScreen* screen);
    void refreshScreens();
    bool startSharing();
    void stopSharing();
    void maximize();
    void displayChanged();
    void rebindCapture();
};
