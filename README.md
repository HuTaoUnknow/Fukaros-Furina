# Fukaros + Furina

Windows 电脑与 Android 手机 / 平板的 USB / Wi-Fi 投屏项目。

当前配套发布为 **v0.4.11**：电脑端 **Fukaros 0.4.10**、移动端 **Furina 0.4.11**。两端兼容并在同一发布页提供安装包。完整变化见 [更新记录](docs/CHANGELOG.md)，构建和实测范围见 [发布验证](docs/VALIDATION.md)。

配对码、平板导航和远程控制使用普通背景；其余必填连接项用深绿色，可选 / 调试连接项用深黄色，并用分割线分开。复制自动使用主屏幕，扩展自动使用平板虚拟屏。每行末尾的“…”使用透明背景、半高按钮及上方竖向弹簧，双端均点击查看步骤、再次点击关闭；移动端设置内容支持手指上下滑动。扩展位置弹窗采用较小的提示和步骤字体。

- **Fukaros**：Windows 桌面端，分享电脑桌面或接收平板画面。
- **Furina**：Android 移动端，接收电脑桌面或分享平板画面。
- 同一时间只使用一个方向；对端点击“同意”后才开始采集与传输。
- 投屏期间锁定方向、复制 / 扩展和连接设置，清晰度可实时调整。发送端空闲画面区显示“占用中...”；点击停止后恢复选择，意外断线仍须先停止。
- 设置分别提供 USB 有线和 Wi-Fi 无线参数页，配对码共用。
- 两端使用一个圆形状态按钮：开始 ▶、等待转圈、连接后停止 ‖。等待时点击可取消，连接后点击可结束连接与投屏。
- 两端用分割线分开方向、复制 / 扩展和清晰度，选择区不显示说明文字；状态和性能详情保留在调试区。
- 电脑端保留系统标题栏，关闭主窗口后进入托盘，继续保持连接和投屏。单击 / 双击托盘或选择“打开 Fukaros”恢复窗口，托盘菜单“退出 Fukaros”结束应用。
- 应用图标采用用户提供的原图：电脑端一根呆毛，移动端三根呆毛，分别用于窗口 / 托盘 / 快捷方式和 Android 启动器。
- 电脑分享支持 **复制 / 扩展**。扩展通过 Windows 虚拟显示驱动创建独立桌面，窗口可拖到平板对应的屏幕。
- 分辨率支持 **1920 / 1080 / 720 / 自动**，数字为最长边像素。默认自动，以 60 FPS 为目标，并过滤短时波动。
- 支持远程点击、滑动、长按及 Android 返回 / 主页 / 最近任务。
- `.ui` 可在 Qt Creator Designer 编辑；协议、传输、编解码、会话确认、分辨率策略等核心代码放在各项目的 `core/`。

## 下载与安装

在 [v0.4.11 发布页](https://github.com/HuTaoUnknow/Fukaros-Furina/releases/tag/v0.4.11) 下载配套的两端安装包；两端应用版本号不同，不影响配对使用：

- [Fukaros-0.4.10-windows-x64-setup.exe](https://github.com/HuTaoUnknow/Fukaros-Furina/releases/download/v0.4.11/Fukaros-0.4.10-windows-x64-setup.exe)：Windows 10 / 11 x64 安装程序，包含 Qt、FFmpeg、独立 ADB 和扩展屏驱动文件。新电脑无需安装 Qt、Qt Creator 或联想模拟器，有快捷方式和卸载入口。
- [Furina-0.4.11-android-arm64.apk](https://github.com/HuTaoUnknow/Fukaros-Furina/releases/download/v0.4.11/Furina-0.4.11-android-arm64.apk)：Android 9 及以上 ARM64 APK。保留 `com.furina.black` 的包标识，名称显示为 Furina，预览包沿用既有测试签名以支持覆盖安装。
- `SHA256SUMS.txt`：安装包校验值。
- `UPDATE-0.4.11.md`：本次更新说明和使用步骤。

## 使用

1. 两端设置相同的配对码（至少 6 位）。
2. **USB（无需调试，0.4.7 起）**：连接数据线，平板在系统设置中开启“USB 网络共享”；Furina 的 USB 页可打开系统设置、刷新 USB IP，返回后按开始。Fukaros 设置选择“USB 有线 → USB 网络（无需调试）”，USB 平板 IP 留空自动发现，两端配对码保持一致。电脑按开始，由对端同意。无需 USB 调试或无线网络；系统不支持 USB 网络共享时可选 ADB 兼容模式。
3. **Wi-Fi**：两端在相互可达的局域网；Furina 的监听地址保留 `0.0.0.0` 并按开始，Fukaros 设置打开“Wi-Fi 无线”页，填平板实际 IPv4 地址并保存。
4. 在 Fukaros 选分享电脑屏幕或获取平板屏幕，按开始；调试区显示等待对端同意。对端点击同意后投屏，拒绝或 30 秒超时会取消请求。
5. 分享平板屏幕还需要 Android 系统录屏授权；远程触控需要开启 Furina 无障碍服务和应用内允许控制选项。
6. 电脑分享选择复制或扩展；首次扩展会请求管理员权限安装签名驱动。首次扩展在虚拟屏就绪后自动打开 Windows 显示设置；拖动屏幕并应用，返回点击“完成并记住位置”后投屏。以后连接选择“继承之前屏幕位置”或“重新配置屏幕位置”。投屏中布局变化会自动重建采集，停止后收起虚拟屏。
7. 等待确认和投屏期间方向及复制 / 扩展不可切换。投屏中使用主界面分辨率按钮调整清晰度；需要更改方式或连接参数时先点击停止。对端点击停止也会结束会话并解锁。
8. 移动端放大后使用系统屏幕边缘返回手势或返回键退出全屏，投屏继续播放；画面中央的普通左滑不会退出。全屏画面不显示额外退出按钮。

Furina 的两个设置页分别展示 USB IP / 系统共享入口和 Wi-Fi 监听地址；USB 页启动时监听所有 IPv4 接口，Wi-Fi 页保留自定义监听地址。配对码在两种传输间共用。USB IP 可在开启网络共享后刷新；USB 设备码仅用于标识，ADB 兼容模式使用电脑下方的 ADB 序列号。

端口为 TCP `27183`，自动发现使用 UDP `27184`。USB 网络模式只探测 Windows 的 USB Ethernet / RNDIS / NCM 接口，并把投屏 TCP 连接绑定到该 USB IPv4 地址，找不到 USB 网卡时不会改用 Wi-Fi。IP 留空时先向 USB 网关和定向广播发送带随机 nonce 的发现报文，发现多个目标时要求指定 USB IP；手填 IP 也必须位于 USB 网段。探测不发送配对码，正式连接沿用挑战认证和对端同意流程。USB ADB 兼容模式仍使用 `adb forward`，Wi-Fi 使用局域网 TCP；各模式共用 H.264 / 60 FPS 目标管线。访客网络 / AP 隔离可能阻止 Wi-Fi 互访。

## 项目结构

```text
Fukaros/                 Windows 桌面端，独立 CMake 项目
  core/                  C 协议、Qt 传输、DXGI / 编解码、虚拟显示驱动
  tests/                 界面与显示源回归测试
  packaging/             Windows 安装包定义及版本资源
  mainwindow.ui          主界面（Qt Designer）
  settings.ui            设置界面（Qt Designer）
Furina/                  Android 移动端，独立 CMake 项目
  core/android/          MediaProjection、MediaCodec、GPU 采样、无障碍控制
  android/               AndroidManifest、Activity、资源和 Gradle Wrapper
  core/                  C 协议、Qt 传输、JNI 和会话逻辑
  tests/                 桌面模拟回归测试
docs/                    发布验证与第三方组件信息
```

两个目录在同一个仓库中，各自可以单独用 Qt Creator 打开 `CMakeLists.txt`。

## 编译

使用 Qt 6.11.1。C 核心使用 C11，Qt 桥接和界面使用 C++17，Android 平台接口使用 Java / JNI。

### Fukaros

安装 Desktop Qt 6.11.1 MinGW 64-bit、MinGW 13.1、CMake 和 Ninja。需要 Qt Widgets、Network；回归测试需要 Qt Test。

```powershell
pwsh -File .\Fukaros\build-desktop.ps1 -QtRoot C:\Qt -Package -PlatformTools C:\Android\Sdk\platform-tools
pwsh -File .\Fukaros\build-installer.ps1 -Iscc 'C:\Tools\InnoSetup7\ISCC.exe'
```

`-Package` 生成包含运行时的 `Fukaros/bin/`；`-PlatformTools` 负责复制 ADB 和许可证。不提供此参数时可以在应用设置中填写已安装的 ADB 路径。安装程序生成于 `Fukaros/dist/`，默认按当前用户安装。

### Furina

需要 Qt 6.11.1 Android ARM64 Kit、相同版本 Desktop Qt、Android SDK platform 36 / build-tools 36.0.0、NDK r27c 27.2.12479018、完整 JDK 21。

```powershell
pwsh -File .\Furina\build-android.ps1 -QtRoot C:\Qt -Sdk C:\Android\Sdk -Ndk C:\Android\Sdk\ndk\27.2.12479018 -Jdk C:\Java\jdk-21 -Abi arm64-v8a
```

脚本默认构建 Debug APK。正式分发时用 `-Configuration Release` 构建，再用 Android build-tools 的 `zipalign` 和 `apksigner` 及自己的签名密钥签名；密钥不提交到 GitHub。APK 路径由脚本输出。

`Furina/build-desktop.ps1` 是界面模拟和回归测试入口，产出的 Windows 程序不能安装到 Android。

## 运行逻辑与边界

Windows 使用 DXGI、GPU 缩放、NVENC H.264；没有可用 NVIDIA 编码器时回退至 Media Foundation。Windows 接收侧使用 FFmpeg / D3D11VA；Android 使用 MediaProjection、GPU 60 Hz 采样、MediaCodec Surface 编解码。

自动分辨率从 1080 开始，以实际编码、接收和显示帧率中的较低值评估：达到 59 FPS 及以上连续 5 秒，按 1080 → 1440 → 1920 逐档试探，1920 为目前最高档。只有低于 50 FPS 连续累计 5 秒才下降一档，恢复到 50 FPS 会清除降档计时。降档后至少观察 10 秒再尝试升档，恢复后可继续探测高档位；编码耗时或队列波动不会单独触发降档。关键帧和 QP 策略尽量降低画质突变。目标 60 FPS，实际帧率受硬件、Wi-Fi 和分辨率影响，不保证所有环境恒定 60 FPS。

局域网视频尚未使用 TLS，配对码通过 HMAC-SHA256 进行认证；只在可信局域网使用。不支持声音、剪贴板、多点触控或完整键盘输入。受保护的 Android 窗口可能黑屏。ADB PNG 预览只用于低帧率调试。

应用改名会迁移旧 `white_furina` / `black_furina` 的本地设置；连接信息和配对码不包含在仓库及安装包中。

第三方代码、驱动与运行时的来源和许可证见 [第三方组件](docs/THIRD_PARTY.md)；验证状态见 [发布验证](docs/VALIDATION.md)。
