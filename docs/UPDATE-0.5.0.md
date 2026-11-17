# Fukaros + Furina v0.5.0

日期：2026-11-17。CMake 工程版本为 **0.5.0**（仓库根目录、Fukaros、Furina 的 `project()`）。这次有意改变线路，0.4.x 对端会被拒绝，没有明文或 HMAC 回退。

安装包见 [v0.5.0](https://github.com/HuTaoUnknow/Fukaros-Furina/releases/tag/v0.5.0)：`Fukaros-0.5.0-windows-x64-setup.exe` 与 `Furina-0.5.0-android-arm64.apk`（Android `versionCode` 24、`versionName` 0.5.0，用与 0.4.x 相同的证书签名，可直接覆盖升级）。两端必须同时升级，0.4.x 与本版不能互连。

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
- 手动配对码至少 6 位，可用任意英文字母和数字（I/O、0/1 不再排除），只拒绝全相同字符。界面在输入时提示。
- Windows 用当前用户的 DPAPI 保存密钥。Android 用 Keystore。旧的明文 `token` 会迁入安全存储后删除。日志和二维码提示不包含密钥内容。

## 配对与连接分离（2026-11-15 修订）

此前首页“开始”在需要配对时又拿配对码当口令，而 `pairingListen` / `pairingConnect` 会在读到已存的 `channel-long-term-key` 时改用那 32 字节，两端各有一把旧密钥就必然握手失败，表现为 “Pairing failed. Connection rejected.”。现在：

- 显式配对永远清掉内存里的长期密钥，配对码或二维码密钥就是 SPAKE2 口令。成功后服务端生成新的 32 字节长期密钥，两端都存。
- 首页“开始”改走新的 `sessionListen` / `sessionConnect`：只认已共享的 32 字节密钥，没有密钥时按钮置灰并提示“请先在设置中完成配对”，点击会打开设置。无密钥时这两个函数直接返回 `false`，不会退化成公开常量口令。
- “配对码匹配”和“扫描二维码”并排放在两端设置页；配对成功后只保存密钥并关闭连接，提示回首页按开始。二维码连接之后同样靠长期密钥识别。
- Wi-Fi 模式下电脑先连设置里的 IP，4 秒失败后由新的 `LanDiscovery` 向本网段广播发现（`FURINA_DISCOVER_V1`，端口 27184），逐个尝试应答者，全部失败时明确提示。以太网和 Wi-Fi 网卡都参与（Wi-Fi 是 `IF_TYPE_IEEE80211`），USB 网络共享网卡不参与。

测试：`pairing` 增加 `sessionListenRequiresAStoredKey`，并把“不同配对码靠旧长期密钥连上”的旧断言反转为必须失败、再验证同码重配对会换新密钥；新增 `lan_discovery` 四项（单应答、应答全部返回、无应答失败、取消后丢弃迟到应答）。两端 `ui` 测试改为注入测试密钥，新增 `startRequiresAPairedKey` 与 `pairCodeButtonPairsAndSavesKey`。

细节见 [配对](security-pairing.md) 与 [加密信道](security-channel.md)。

细节见 [配对](security-pairing.md)。

## 提权助手与 Linux CI

- 虚拟显示助手不再从当前用户可写的安装目录直接提权。先核对构建时写入的 SHA-256，必要时复制到受保护目录并收紧权限，再启动副本。
- 下一次启动会清理已退出的安装进程、临时文件，以及确认是截断模板的显示配置。已经完整的设置会保留。
- GitHub Actions 在 Ubuntu 上配置仓库并运行 `ctest`。不构建 Android APK，不运行 NVENC，不安装 IDD。

细节见 [提权助手](security-helper.md)。

## 这次在 Linux 上跑过的测试

环境：Ubuntu 24.04，g++ 13，Qt 6.11.1，`cmake -G Ninja -DCMAKE_BUILD_TYPE=Release`，然后 `ctest --output-on-failure`。共 19 项。

19 项全部通过，包括 `ui`、`ui_multi_screen`、`usb_network` 和 `black_ui`。`pairing` 覆盖了手动配对码、一次性二维码密钥进入 FUR2、长期密钥覆盖不同配对码、错误配对码失败、`FUR1` 被拒绝，以及二维码公钥等于信道 X25519 公钥。

`black_ui` 的 `portraitLayoutAndConsent` 在未改动的 `main` 上用同一套 Qt 6.11.1 可以通过。0.5.0 上曾失败：IO 线程先把 `ready()` 清掉，界面线程上晚到的断开又退出了已经开始的全屏，竖屏预览停在 500 像素，中心控件是 390×780。断开时只有仍在进行的会话才会退出全屏并清画面。修复后整组 `black_ui` 通过。

`ui`、`ui_multi_screen` 的 `fullscreenRestoresWindow` 和 `usb_network` 的发现用例在未改动的 `main` 上同样失败，断言不适合这套 Linux / Qt，不是 0.5.0 的产品回归：

- offscreen 会先把窗口标成全屏，侧栏虽然已经隐藏，48 像素的位置要等屏幕尺寸应用后才收回。测试改为等到画面原点回到中心控件的 `(0,0)`。
- Linux 回环会把绑定在 `127.0.0.3` 上的 UDP 应答源改写成 `127.0.0.1`。产品报告的是这个真实源地址。测试在 Linux 上期望 `127.0.0.1`，并仍用发现到的地址完成绑定连接；Windows 仍期望 `127.0.0.3`。

`ui` 在 Windows 上仍会跳过托盘用例。上述两处只改了测试等待和 Linux 回环上的期望源地址，没有改产品行为。

## 平板上的相机扫码

2026-11-15 在小米平板（liuqin，Android 15，1800×2880，density 400）上用 USB 调试安装 Debug APK，验证了相机扫码。

- 相机权限由 `FurinaActivity` 自己申请，不再走 `QCoreApplication::requestPermission`。后者会经 QtAndroidAccessibility 做对象上下文检查，开启无障碍服务时与 `requestPermissionsInternal` 互锁，权限被静默判为拒绝。
- 预览方向按 `pairingCameraUprightDegrees` 算出直立角度，再减去相机 HAL 的 SurfaceTexture 变换（`halQuarterTurns()`）。这块设备的 HAL 固定做一次轴转置，不减掉的话预览会多转 90°。
- 满幅缩放按实际施加的角度算，不是按直立角度算。横屏时两者差一个直角，按直立角度算会在左右各留约 12.5% 黑边。
- 解码按直立于首选角度、再依次试其余三个直角，因此厂商对“缓冲区是否已经直立”的分歧不影响识别。
- 实测日志：横屏 `display=ROTATION_90 sensor=90 upright=0 hal=90 applied=270 view=2880x1800 cover=2.4`；竖屏 `display=ROTATION_0 sensor=90 upright=90 hal=90 applied=0 view=1800x2880 cover=2.4`。两种方向截图四边均无黑边。
- 未在这台设备上完成一次端到端配对：扫码界面正常出图与解码循环运行，但没有在电脑端同时显示二维码做完整握手。

## 没有在硬件上运行

- 没有 Windows 机器，没有安装或测试 IDD 虚拟显示驱动，没有跑 NVENC。
- 没有核对 DPAPI 和 Android Keystore 的实机读写。

Windows 安装包和 Android APK 已于 2026-11-17 制作，见本页开头。
