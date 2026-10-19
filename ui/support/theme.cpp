#include "interface_support.h"

QString furinaStyle() {
    return QStringLiteral(R"(
 QWidget { background:#111820; color:#e4eaf0; font-family:"Microsoft YaHei","Noto Sans CJK SC",sans-serif; font-size:13px; }
 QFrame#sidebar { background:#1a232e; border-right:1px solid #34404e; }
 QWidget#bottomPanel { background:#17212c; }
 QPushButton { background:#253344; border:1px solid #435264; border-radius:6px; padding:7px 10px; min-height:26px; }
 QPushButton:hover { background:#33465d; }
 QPushButton:pressed { background:#406184; }
 QPushButton:disabled { color:#768393; border-color:#283542; }
 QPushButton#start,QPushButton#settings,QPushButton#maximize { min-height:36px; padding:6px 0px; }
 QPushButton#start,QPushButton#settings,QPushButton#maximize { min-width:38px; min-height:38px; max-width:38px; max-height:38px; padding:0px; border-radius:20px; font-size:12px; }
 QPushButton#exitFullScreen { background:#253344; }
 QWidget#windowControls { background:#17212c; }
 QWidget#windowControls QPushButton { min-width:32px; max-width:32px; min-height:26px; max-height:26px; padding:0; border:0; border-radius:0; }
 QPushButton#windowClose:hover { background:#b83242; }
 QFrame#consentPanel { background:#253344; border:1px solid #57b9d8; border-radius:4px; }
 QWidget#resolutionPanel QPushButton { min-height:18px; padding:3px 5px; border-radius:4px; font-size:11px; }
 QWidget#resolutionPanel QPushButton:checked { background:#216e89; border-color:#57b9d8; }
 QLabel#performanceHint { color:#98afc6; font-size:11px; }
 QLineEdit,QComboBox { background:#0c121a; border:1px solid #394b5e; border-radius:5px; padding:7px; min-height:24px; }
 QComboBox::drop-down { border:0px; border-left:1px solid #435264; background:#253344; width:24px; }
 QFrame#modeSeparator,QFrame#resolutionSeparator { border:0px; border-top:1px solid #435264; min-height:1px; max-height:1px; }
 QFrame#settingsSeparator { border:0px; border-top:1px solid #5c758d; min-height:8px; max-height:8px; margin-top:6px; }
 QGroupBox { border:1px solid #394b5e; border-radius:5px; margin-top:12px; padding:12px 8px 8px; }
 QGroupBox::title { subcontrol-origin:margin; left:10px; padding:0 4px; }
 QTabWidget::pane { border:1px solid #394b5e; border-radius:5px; padding:8px; }
 QTabBar::tab { background:#253344; border:1px solid #394b5e; padding:9px 16px; }
 QTabBar::tab:selected { background:#216e89; }
 QTabBar::tab:disabled,QCheckBox:disabled,QLineEdit:disabled { color:#768393; }
 QLabel#sessionSettingsHint { color:#f0c779; }
 QRadioButton { padding:6px 0px; }
 QRadioButton::indicator { width:16px; height:16px; border:1px solid #8096ae; border-radius:8px; background:#0c121a; }
 QRadioButton::indicator:checked { background:#57b9d8; border:3px solid #244658; }
 QCheckBox { padding:5px 0px; }
 QCheckBox::indicator { width:16px; height:16px; border:1px solid #8096ae; border-radius:3px; background:#0c121a; }
 QCheckBox::indicator:checked { background:#57b9d8; border:1px solid #a5e8fa; }
 QPlainTextEdit { background:#0b1119; border:1px solid #344354; border-radius:5px; padding:5px; font-size:12px; }
 QLabel#debugTitle,QLabel#directionTitle { color:#98afc6; font-size:12px; }
 QLabel#castHint { color:#b5c6d7; font-size:12px; }
 QScrollArea { border:0px; }
 QScrollBar:vertical { width:8px; background:#17212c; }
 QScrollBar::handle:vertical { background:#415267; min-height:25px; }
 QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0px; }
 QScrollBar:horizontal { height:8px; background:#17212c; }
 QScrollBar::handle:horizontal { background:#415267; min-width:25px; }
 QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal { width:0px; }

 QGroupBox[settingsRole="required"] { background:#14251f; border-color:#3d6453; }
 QGroupBox[settingsRole="optional"] { background:#27241c; border-color:#6c6240; }
 QGroupBox[settingsRole="required"] QLabel { background:transparent; color:#d9e6df; }
 QGroupBox[settingsRole="optional"] QLabel,QGroupBox[settingsRole="optional"] QCheckBox { background:transparent; color:#e4ddc7; }
 QLineEdit[settingsRole="required"] { background:#1a3027; border-color:#4f8068; color:#e4eee8; }
 QLineEdit[settingsRole="optional"],QGroupBox[settingsRole="optional"] QComboBox { background:#302b20; border-color:#6c6240; color:#eae4d3; }
 QLineEdit[settingsRole="required"]:focus { border:2px solid #6f9b81; }
 QLineEdit[settingsRole="optional"]:focus { border:2px solid #9b8b55; }
 QPushButton[stepHelp="true"] { background:transparent; border:0; min-width:32px; max-width:32px; min-height:16px; max-height:16px; padding:0; font-size:16px; }
 QPushButton[stepHelp="true"]:hover { background:transparent; color:#a5e8fa; }
 QPushButton[stepHelp="true"]:pressed { background:transparent; color:#57b9d8; }
 QFrame[stepPopup="true"] { background:#203044; border:1px solid #77bdd9; border-radius:8px; }
 QFrame[stepPopup="true"] QWidget { background:transparent; }
 QLabel#stepsTitle { font-size:15px; font-weight:bold; color:#f5faff; }
 QLabel#stepsBody { color:#eaf3ff; padding:2px; }
 QPushButton#closeSteps { min-width:32px; max-width:32px; min-height:32px; max-height:32px; padding:0; font-size:20px; }
 QFrame#usbSeparator,QFrame#wifiSeparator { border:0; border-top:1px solid #73837b; min-height:1px; max-height:1px; }
)");
}
