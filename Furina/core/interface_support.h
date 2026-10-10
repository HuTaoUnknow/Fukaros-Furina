#pragma once
#include <QObject>
#include <QMainWindow>
#include <QPushButton>
#include <QComboBox>
#include <QListWidget>
#include <QPointer>
#include <QButtonGroup>
#include <QCheckBox>
#include "screenview.h"
QString furinaStyle();
void furinaConfigureToolIcons(QPushButton *settings,QPushButton *maximize);
void furinaSetMaximizeIcon(QPushButton *button,bool restore);
class DirectionChoice : public QObject {
 Q_OBJECT
public:
 explicit DirectionChoice(QWidget *parent);
 bool sharing()const;void setSharing(bool sharing);
signals:void changed();
private:QCheckBox *m_share,*m_receive;QButtonGroup *m_group;
};
class ResolutionChoice:public QObject {
 Q_OBJECT
public:
 explicit ResolutionChoice(QWidget *parent);int value()const{return m_value;}void setValue(int edge);
signals:void changed(int edge);
private:QButtonGroup *m_group;int m_value=0;
};
QString furinaLog(const QString &message, const QString &english = {});
QString furinaStatus(const QString &chinese, const QString &english);
class FurinaComboBox : public QComboBox {
public:
 explicit FurinaComboBox(QWidget *parent=nullptr):QComboBox(parent){}
 void showPopup() override;
 void hidePopup() override;
protected:
 void paintEvent(QPaintEvent *event) override;
 bool eventFilter(QObject *watched,QEvent *event) override;
private:
 QPointer<QListWidget> m_popup;
};
class Presentation : public QObject {
 Q_OBJECT
public:
 Presentation(QMainWindow *window,QWidget *sidebar,QWidget *controls,ScreenView *view);
 bool active() const{return m_active;}
 void setActive(bool on);
signals:
 void changed(bool active);
protected:
 bool eventFilter(QObject *watched,QEvent *event) override;
private:
 QMainWindow *m_window;QWidget *m_sidebar,*m_controls;ScreenView *m_view;QPushButton *m_exit;
 bool m_active=false,m_wasMaximized=false;QRect m_geometry;
#ifdef Q_OS_WIN
 Qt::WindowFlags m_windowFlags;quint32 m_borderColor=0xffffffffu,m_cornerPreference=0;
#endif
};
class WindowControls : public QObject {
 Q_OBJECT
public:
 WindowControls(QMainWindow *window,Presentation *presentation,ScreenView *view,QWidget *controls);
protected:bool eventFilter(QObject *watched,QEvent *event) override;
private:
 QMainWindow *m_window;Presentation *m_presentation;ScreenView *m_view;QWidget *m_controls;
 void arrange();void refresh();
};
