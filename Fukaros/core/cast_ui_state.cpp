#include "cast_ui_state.h"
#include <QDialogButtonBox>
#include <QSignalBlocker>

CastUiState::CastUiState(QWidget *window):QObject(window),
 m_share(window->findChild<QCheckBox*>("share")),m_receive(window->findChild<QCheckBox*>("receive")),
 m_duplicate(window->findChild<QCheckBox*>("duplicate")),m_extend(window->findChild<QCheckBox*>("extend")),
 m_view(window->findChild<ScreenView*>()),m_window(window),
 m_save(window->findChild<QDialogButtonBox*>("buttonBox")->button(QDialogButtonBox::Save)),m_consent(window->findChild<SessionConsent*>()) {
 for(auto *button:{m_share,m_receive,m_duplicate,m_extend})
  connect(button,&QCheckBox::toggled,this,[this]{if(m_locked&&!m_restoring)preserveSelection();});
 updateControls();
}
void CastUiState::updateControls(){
 m_consent->setRequestsBlocked(m_occupied);
 const QString hint=m_locked?QStringLiteral("投屏方式已锁定，请先点击停止。\nCasting options are locked. Press Stop first."):QString();
 for(auto *button:{m_share,m_receive,m_duplicate,m_extend}){button->setEnabled(!m_locked);button->setToolTip(hint);}
 for(const auto *name:{"pairingGroup","transport","connectionTabs","displayGroup","controlGroup"})
  if(auto *group=m_window->findChild<QWidget*>(name))group->setEnabled(!m_locked);
 m_save->setEnabled(!m_locked);
 if(auto *hintLabel=m_window->findChild<QWidget*>("sessionSettingsHint"))hintLabel->setVisible(m_locked);
}
void CastUiState::preserveSelection(){
 if(!m_locked||m_restoring)return;
 m_restoring=true;
 const QSignalBlocker a(m_share),b(m_receive),c(m_duplicate),d(m_extend);
 (m_sharing?m_share:m_receive)->setChecked(true);
 (m_extended?m_extend:m_duplicate)->setChecked(true);
 m_restoring=false;
}
void CastUiState::lockPending(){
 if(m_locked)return;
 m_sharing=m_share->isChecked();m_extended=m_extend->isChecked();m_locked=true;updateControls();
}
void CastUiState::occupy(bool sharing,bool extended){
 m_sharing=sharing;m_extended=extended;m_locked=true;m_occupied=true;
 preserveSelection();updateControls();m_view->setBusy(sharing);
}
void CastUiState::reset(){m_locked=false;m_occupied=false;m_view->setBusy(false);updateControls();}
void CastUiState::connectionLost(){if(!m_occupied)reset();}
