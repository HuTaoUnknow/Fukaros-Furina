#include "debug_log.h"
#include "debug_text.h"
#include <QDateTime>

QString furinaStatus(const QString& cn, const QString& en) {
    return cn + "\nEN: " + en;
}

QString furinaLog(const QString& message, const QString& english) {
    QString cn = message.trimmed(), en = english.trimmed();
    if (cn.isEmpty())
        return {};
    const auto separator = cn.indexOf("\nEN: ");
    if (separator >= 0) {
        en = cn.mid(separator + 5);
        cn = cn.left(separator);
    }

    struct Pair {
        const char* cn;
        const char* en;
    };

    static const Pair translations[] = {
        {"视频数据无效", "Invalid video packet"},
        {"视频档位无效", "Invalid video profile"},
        {"视频反馈无效", "Invalid video feedback"},
        {"配对成功", "Paired"},
        {"连接已断开", "Disconnected"},
        {"连接超时", "Connection timeout"},
        {"配对码至少需要 6 位", "Pairing code requires at least 6 characters"},
        {"请输入平板配对码（至少 6 位）", "Enter the tablet pairing code (at least 6 characters)"},
        {"配对码错误", "Pairing code rejected"},
        {"需要先完成认证", "Authentication required"},
        {"接收缓存超出限制", "Receive buffer exceeded"},
        {"协议头无效", "Invalid protocol header"},
        {"画面尺寸无效", "Invalid frame dimensions"},
        {"JPEG 图像无效", "Invalid JPEG"},
        {"触控手势无效", "Invalid gesture"},
        {"按键无效", "Invalid key"},
        {"状态消息无效", "Invalid status"},
        {"心跳请求无效", "Invalid ping"},
        {"心跳响应无效", "Invalid pong"},
        {"收到未知消息", "Unexpected message"},
        {"接收端处理过慢", "Peer too slow"},
        {"投屏请求无效", "Invalid session request"},
        {"监听 IP 地址无效", "Invalid listening IP address"},
        {"ADB 操作超时", "ADB timed out"},
        {"请先填写 USB 设备码", "Select a device serial first"},
        {"ADB 队列已满", "ADB queue is full"},
        {"ADB 命令失败", "ADB command failed"},
        {"远端已关闭连接", "The remote host closed the connection"},
        {"网络操作超时", "Network operation timed out"},
        {"套接字操作超时", "Socket operation timed out"},
        {"网络不可达", "Network unreachable"},
        {"此操作的代理类型无效，请检查系统代理", "The proxy type is invalid for this operation"},
        {"地址已被占用", "The bound address is already in use"},
        {"没有可用的硬件编码器", "No hardware encoder"},
        {"连接被拒绝，请检查对端服务是否启动", "Connection refused"},
        {"找不到设备地址", "Host not found"},
        {"录屏授权已取消", "Screen capture permission was cancelled"},
        {"平板录屏已启动", "Tablet screen capture started"},
        {"系统已停止屏幕共享", "System stopped screen sharing"},
        {"请在平板设置中启用 Furina 无障碍服务", "Enable Furina accessibility service in tablet settings"},
        {"上一手势尚未完成", "Previous gesture has not completed"},
        {"系统取消了触控手势", "System cancelled the gesture"},
        {"系统拒绝了触控手势", "System rejected the gesture"},
        {"无障碍服务未开启", "Accessibility service is disabled"},
        {"系统拒绝了导航操作", "System rejected the navigation action"},
        {"桌面模拟：使用当前主屏幕代替 Android 录屏",
         "Desktop simulation: using the primary monitor for capture"},
        {"无障碍触控仅在 Android 可用；桌面模拟只记录控制事件",
         "Accessibility is available on Android; desktop simulation only logs input"}};
    if (en.isEmpty())
        for (const auto& p : translations) {
            if (cn == QString::fromUtf8(p.cn) || cn == QString::fromUtf8(p.en)) {
                cn = QString::fromUtf8(p.cn);
                en = QString::fromUtf8(p.en);
                break;
            }
        }
    if (en.isEmpty() && cn.startsWith("Listening on port ")) {
        en = cn;
        cn = "正在监听端口 " + cn.mid(18);
    }
    if (en.isEmpty()) {
        static const Pair prefixes[] = {{"视频编码帧失败：", "Video encoding failed: "},
                                        {"硬件编码错误：", "Hardware encoder error: "},
                                        {"视频解码失败：", "Video decode failed: "},
                                        {"GPU 录屏失败：", "GPU capture failed: "},
                                        {"录屏启动失败：", "Starting capture failed: "},
                                        {"分辨率切换失败：", "Resolution change failed: "},
                                        {"Android H.264 硬件编码：", "Android hardware H.264 encoding: "},
                                        {"Android H.264 硬件解码：", "Android hardware H.264 decoding: "},
                                        {"视频数据无效", "Invalid video packet"},
                                        {"视频档位无效", "Invalid video profile"},
                                        {"视频反馈无效", "Invalid video feedback"},
                                        {"录屏授权失败：", "Screen capture permission failed: "},
                                        {"启动录屏失败：", "Starting screen capture failed: "},
                                        {"录屏失败：", "Screen capture failed: "},
                                        {"录屏帧处理失败：", "Processing captured frame failed: "},
                                        {"模拟手势 ", "Simulated gesture "},
                                        {"模拟按键 ", "Simulated key "}};
        for (const auto& p : prefixes)
            if (cn.startsWith(QString::fromUtf8(p.cn))) {
                en = QString::fromUtf8(p.en) + cn.mid(QString::fromUtf8(p.cn).size());
                break;
            }
    }
    en.replace("，目标 60 FPS", ", target 60 FPS");
    if (en.isEmpty()) {
        en = "Device/system report: " + cn;
        cn = "设备 / 系统原始信息：" + cn;
    }
    cn = furinaChineseDebugParameters(cn);
    cn.replace('\n', " | ");
    en.replace('\n', " | ");
    const auto stamp = QDateTime::currentDateTime().toString("[HH:mm:ss] ");
    return stamp + "中 · " + cn + '\n' + stamp + "EN · " + en;
}
