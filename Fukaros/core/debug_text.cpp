#include "debug_text.h"
#include <QRegularExpression>

QString furinaChineseDebugParameters(QString text) {
 struct Term {const char *english;const char *chinese;};
 // Keep values (device IDs, addresses, codec names and error codes) intact.
 static const Term parameters[]={
  {"width","宽度"},{"height","高度"},{"resolution","分辨率"},{"fps","帧率"},
  {"shown","显示帧率"},{"queueMs","队列时长"},{"queue","队列"},{"bitrate","码率"},
  {"target","目标"},{"edge","最长边"},{"epoch","配置版本"},{"sequence","帧序号"},
  {"codec","编解码器"},{"encoder","编码器"},{"decoder","解码器"},
  {"latency","延迟"},{"elapsed","已用时间"},{"workMs","处理耗时"},{"frames","帧数"},
  {"format","格式"},{"port","端口"},{"size","尺寸"}
 };
 for(const auto &term:parameters){
  const QRegularExpression key(QString("(?<![\\w.])%1(?=\\s*[=:：])").arg(term.english),QRegularExpression::CaseInsensitiveOption);
  text.replace(key,QString::fromUtf8(term.chinese));
 }
 static const Term terms[]={
  {"FPS","帧/秒"},{"GPU","图形处理器"},{"CPU","中央处理器"},
  {"NVENC","显卡硬件编码"},{"D3D11VA","显卡硬件解码"},{"DXGI","桌面图像采集接口"},
  {"Media Foundation","系统媒体框架"},{"Android","安卓"},
  {"Wi-Fi","无线网络"},{"USB","有线"},{"ADB","安卓调试桥"},{"IP","网络地址"}
 };
 for(const auto &term:terms)
  text.replace(QRegularExpression(QString("(?<![\\w.])%1(?![\\w.])").arg(QRegularExpression::escape(term.english)),QRegularExpression::CaseInsensitiveOption),QString::fromUtf8(term.chinese));
 text.replace(QRegularExpression("(\\d(?:[\\d.]*)?)\\s*ms\\b",QRegularExpression::CaseInsensitiveOption),"\\1 毫秒");
 text.replace(QRegularExpression("(\\d(?:[\\d.]*)?)\\s*Hz\\b",QRegularExpression::CaseInsensitiveOption),"\\1 赫兹");
 text.replace(QRegularExpression("(\\d(?:[\\d.]*)?)\\s*Mbps\\b",QRegularExpression::CaseInsensitiveOption),"\\1 兆比特/秒");
 text.replace(QRegularExpression("(\\d(?:[\\d.]*)?)\\s*kbps\\b",QRegularExpression::CaseInsensitiveOption),"\\1 千比特/秒");
 text.replace(QRegularExpression("(\\d(?:[\\d.]*)?)\\s*px\\b",QRegularExpression::CaseInsensitiveOption),"\\1 像素");
 return text;
}
