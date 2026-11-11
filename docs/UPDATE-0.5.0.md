# Fukaros + Furina v0.5.0

日期：2026-11-11。CMake 工程版本为 **0.5.0**（仓库根目录、Fukaros、Furina 的 `project()`）。这次有意改变线路，0.4.x 对端会被拒绝，没有明文或 HMAC 回退。

还没有新的 Windows 安装包和 Android APK。安装脚本和 Android `versionName` 仍写着 0.4.20。已发布的安装包仍是 [v0.4.20](https://github.com/HuTaoUnknow/Fukaros-Furina/releases/tag/v0.4.20)。

## 加密信道

- 数据帧之前先做 FUR2 握手。横幅是 4 字节 `FUR2` 加版本号 2。版本不对，或者对端仍是 0.4.x 的 `FUR1`，直接拒绝。
- 互相认证使用 SPAKE2（RFC 9382）。口令不出现在报文里，也不发送 `HMAC(challenge, password)`。被动记录不能离线枚举口令。
- 握手之后，原来的 FUR1 帧放进 XChaCha20-Poly1305。主连接和独立鼠标连接都走这套保护。重放和乱序会失败。重连使用新的计数器。
- 实现放在仓库里的 Monocypher 4.0.2，加上 `core/crypto/` 的 SPAKE2。没有改用 `QSslSocket`。
- 监听地址按调用方给出的地址绑定，不会在指定地址失败时改听 `0.0.0.0`。同一时间只保留一路主会话。握手失败按远端地址和全局计数，5 次后锁定，之后加长。
- 视频加密和写出不在界面线程。尚未写完的视频帧最多 3 个，再多就丢弃并等待下一个关键帧。
- 第一次配对成功后得到 32 字节长期密钥。手动重连会使用它。一次性二维码那一次仍使用二维码里的密钥。

细节见 [加密信道](security-channel.md)。

## 配对

- 电脑显示二维码，平板扫描。文本为 `fukaros://pair?v=2&h=HOST&p=PORT&s=ONE_TIME_SECRET&f=FINGERPRINT&e=UNIX_EXPIRY`，约 2 分钟内有效，同一密钥不能再用。
- 字段 `s` 是 128 位一次性密钥。`pairingListen` / `pairingConnect` 把它加上 `ot1.` 前缀后交给 FUR2，作为这一次的 SPAKE2 口令，不安装成 HMAC 密钥。
- 字段 `f` 是桌面 X25519 公钥的 SHA-256。私钥存在安全存储的 `desktop-x25519-secret`，公钥由信道里的 `furinaX25519PublicKey` 算出。平板在发出流量前固定这个指纹。
- 手动配对码至少 12 位，字母表不含 I、O 和容易混淆的数字，须同时有字母和数字，并拒绝全相同字符和简单顺子。界面在输入时提示。
- Windows 用当前用户的 DPAPI 保存密钥。Android 用 Keystore。旧的明文 `token` 会迁入安全存储后删除。日志和二维码提示不包含密钥内容。

细节见 [配对](security-pairing.md)。

## 提权助手与 Linux CI

- 虚拟显示助手不再从当前用户可写的安装目录直接提权。先核对构建时写入的 SHA-256，必要时复制到受保护目录并收紧权限，再启动副本。
- 下一次启动会清理已退出的安装进程、临时文件，以及确认是截断模板的显示配置。已经完整的设置会保留。
- GitHub Actions 在 Ubuntu 上配置仓库并运行 `ctest`。不构建 Android APK，不运行 NVENC，不安装 IDD。

细节见 [提权助手](security-helper.md)。

## 这次在 Linux 上跑过的测试

环境：Ubuntu 24.04，g++ 13，Qt 6.11.1，`cmake -G Ninja -DCMAKE_BUILD_TYPE=Release`，然后 `ctest --output-on-failure`。共 19 项。

通过：`single_instance`、`protocol`、`cast_source`、`peer`、`crypto`、`pairing`、`session_consent`、`video_policy`、`display_position`、`settings_support`、`pointer_gain`、`helper_launch`、`pointer_settings`、`screen_scale`、`mobile_video_policy`。`pairing` 覆盖了手动配对码、一次性二维码密钥进入 FUR2、长期密钥覆盖不同配对码、错误配对码失败、`FUR1` 被拒绝，以及二维码公钥等于信道 X25519 公钥。

下列失败在 v0.4.20 上已经存在，这次没有改断言：

- `ui` 和 `ui_multi_screen` 的 `fullscreenRestoresWindow`：`view->mapTo(...)` 得到 `QPoint(48,0)`，期望 `QPoint(0,0)`。
- `usb_network` 的 `discoversAndPairsThroughBoundInterface`：发现结果来源是 `127.0.0.1`，期望 `127.0.0.3`。失败发生在 `Peer::listen` 之前。
- `black_ui` 的 `portraitLayoutAndConsent`：全屏后 `view` 为 `390x500`，中心控件为 `390x780`。此前记录是 Qt 6.4.2 失败、Qt 6.11.1 通过。这次是 Linux offscreen 上的 Qt 6.11.1，仍然失败。没有放宽断言。

`ui` 在 Windows 上跳过托盘用例。没有把这些失败记成 0.5.0 的回归。

## 没有在硬件上运行

- 没有 Windows 机器，没有安装或测试 IDD 虚拟显示驱动，没有跑 NVENC。
- 没有平板或手机，没有相机扫码，没有 Android 模拟器或 APK。
- 没有制作安装包，也没有核对 DPAPI 和 Android Keystore 的实机读写。
