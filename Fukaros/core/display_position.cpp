#include "display_position.h"
#include "ui_position_dialog.h"
#include <QDialog>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>

DisplayPosition::DisplayPosition(QWidget *window,Read read,Apply apply,OpenSettings openSettings):QObject(window),ui(new Ui::PositionDialog),m_dialog(new QDialog(window)),m_read(std::move(read)),m_apply(std::move(apply)),m_openSettings(std::move(openSettings)){
 ui->setupUi(m_dialog);m_dialog->setWindowModality(Qt::NonModal);
 if(!m_openSettings)m_openSettings=[](){return QDesktopServices::openUrl(QUrl("ms-settings:display"));};
 connect(ui->inheritPosition,&QPushButton::clicked,this,&DisplayPosition::inherit);
 connect(ui->reconfigurePosition,&QPushButton::clicked,this,&DisplayPosition::openConfiguration);
 connect(ui->openDisplaySettings,&QPushButton::clicked,this,&DisplayPosition::openConfiguration);
 connect(ui->finishPosition,&QPushButton::clicked,this,&DisplayPosition::complete);
 connect(ui->cancelPosition,&QPushButton::clicked,m_dialog,&QDialog::reject);
 connect(m_dialog,&QDialog::finished,this,[this]{if(m_active){m_active=false;m_configured=false;emit cancelled();}});
}
DisplayPosition::~DisplayPosition(){reset();delete ui;delete m_dialog;}
bool DisplayPosition::savedPosition(QPoint *position)const{
 QSettings settings;bool xOk=false,yOk=false;
 const int x=settings.value("extensionPosition/x").toInt(&xOk),y=settings.value("extensionPosition/y").toInt(&yOk);
 if(!settings.value("extensionPosition/valid",false).toBool()||!xOk||!yOk)return false;
 *position=QPoint(x,y);return true;
}
bool DisplayPosition::store(const QPoint &position){
 QSettings settings;settings.setValue("extensionPosition/x",position.x());settings.setValue("extensionPosition/y",position.y());settings.setValue("extensionPosition/valid",true);settings.sync();
 return settings.status()==QSettings::NoError;
}
void DisplayPosition::begin(){
 if(m_active||m_configured)return;
 m_active=true;ui->positionError->clear();ui->positionError->hide();
 QPoint previous;ui->positionPages->setCurrentWidget(savedPosition(&previous)?ui->choicePage:ui->configurePage);
 // The main Stop button must remain usable while choosing the layout.
 m_dialog->show();m_dialog->raise();
 if(ui->positionPages->currentWidget()==ui->configurePage)openConfiguration();
 else emit status("请选择继承之前屏幕位置或重新配置屏幕位置。","Choose to reuse the previous screen position or configure it again.");
}
void DisplayPosition::openConfiguration(){
 if(!m_active)return;
 ui->positionPages->setCurrentWidget(ui->configurePage);ui->positionError->hide();
 emit status("等待配置扩展屏位置：在系统显示设置中拖动屏幕并应用，返回后点击完成。","Waiting for the extension position: arrange the screen in Windows Display Settings, apply, then return and finish.");
 if(!m_openSettings()){
  ui->positionError->setText("未能打开显示设置，请进入 Windows 系统 → 显示。");ui->positionError->show();
  emit status("打开系统显示设置失败，请手动打开。","Could not open Windows Display Settings. Open it manually.");
 }
}
void DisplayPosition::inherit(){
 if(!m_active)return;
 QPoint previous;
 if(!savedPosition(&previous)||!m_apply(previous)){
  emit status("之前的位置无法用于当前显示器布局，请重新配置。","The saved position cannot be used with the current monitor layout. Configure it again.");
  openConfiguration();return;
 }
 // Confirm the OS accepted the requested position, rather than trusting a
 // successfully returned call that normalized it to another layout.
 QPoint actual;if(!m_read(&actual)||actual!=previous){openConfiguration();return;}
 emit status("已继承之前屏幕位置。","Previous screen position restored.");finish();
}
void DisplayPosition::complete(){
 if(!m_active)return;
 QPoint position;
 if(!m_read(&position)){
  ui->positionError->setText("无法读取平板扩展屏位置，请确认该屏幕仍处于扩展模式。");ui->positionError->show();return;
 }
 if(!store(position)){
  ui->positionError->setText("屏幕位置保存失败，请重试。");ui->positionError->show();return;
 }
 emit status("扩展屏位置已记住。","Extended screen position saved.");finish();
}
void DisplayPosition::finish(){m_active=false;m_configured=true;m_dialog->accept();emit ready();}
void DisplayPosition::rememberCurrent(){if(!m_configured)return;QPoint position;if(m_read(&position))store(position);}
void DisplayPosition::reset(){m_active=false;m_configured=false;m_dialog->hide();}
