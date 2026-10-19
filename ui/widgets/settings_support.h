#pragma once
#include <QPushButton>
#include <QPointer>
#include <QFrame>

class QScrollArea;

// Editable in Qt Designer as a promoted QPushButton. The popup is a child
// widget on Android's existing activity surface, not a second native window.
class StepHelpButton : public QPushButton {
    Q_OBJECT
    Q_PROPERTY(QString helpTitle READ helpTitle WRITE setHelpTitle)
    Q_PROPERTY(QString steps READ steps WRITE setSteps)
  public:
    explicit StepHelpButton(QWidget* parent = nullptr);

    QString helpTitle() const {
        return m_title;
    }

    QString steps() const {
        return m_steps;
    }

    void setHelpTitle(const QString& title);
    void setSteps(const QString& steps);

  protected:
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    QString m_title, m_steps;
    QPointer<QFrame> m_popup;
    void showHelp();
    void hideHelp();
};

void furinaConfigureSettingsScroll(QScrollArea* scroll);
