#pragma once
#include <QObject>
#include <QPointer>
#include <QIcon>

class QWidget;
class QSystemTrayIcon;
class QMenu;

class DesktopTray final : public QObject {
    Q_OBJECT
  public:
    DesktopTray(QWidget* window, const QIcon& icon);
    bool available() const;
    void restoreWindow();
  signals:
    void hiddenToTray();
    void quitRequested();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    QPointer<QWidget> m_window;
    QSystemTrayIcon* m_tray;
    QMenu* m_menu;
    bool m_quitting = false;
};
