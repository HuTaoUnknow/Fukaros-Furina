#pragma once
#include <QObject>
#include <QPoint>
#include <functional>

class QWidget;
class QDialog;
namespace Ui {class PositionDialog;}

// Position is stored in Windows desktop pixels, outside the settings UI.
class DisplayPosition:public QObject {
 Q_OBJECT
public:
 using Read=std::function<bool(QPoint*)>;
 using Apply=std::function<bool(const QPoint&)>;
 using OpenSettings=std::function<bool()>;
 DisplayPosition(QWidget *window,Read read,Apply apply,OpenSettings openSettings={});
 ~DisplayPosition() override;
 void begin();
 void reset();
 void rememberCurrent();
 bool active()const{return m_active;}
 bool configured()const{return m_configured;}
signals:
 void ready();
 void cancelled();
 void status(const QString &chinese,const QString &english);
private:
 Ui::PositionDialog *ui;QDialog *m_dialog;
 Read m_read;Apply m_apply;OpenSettings m_openSettings;
 bool m_active=false,m_configured=false;
 void openConfiguration();void complete();void inherit();
 bool savedPosition(QPoint *position)const;
 bool store(const QPoint &position);
 void finish();
};
