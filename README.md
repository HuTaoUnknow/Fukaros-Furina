# Fukaros + Furina

Windows 电脑与 Android 手机 / 平板之间的 USB / Wi-Fi 双向投屏项目。电脑端 **Fukaros** 与移动端 **Furina** 在同一仓库维护，当前配套稳定版为 **v0.4.20**。

v0.4.20 新增双端投屏方向记忆、同步的“副屏放大”开关与比例（默认开启、150%，仅扩展桌面），并修复安卓解码积压反复重建及独立鼠标通道低延迟设置。2026-10-26 同版本覆盖更新，加入 Windows 退出全屏后标题栏恢复修复。详见 [0.4.20 更新说明](docs/UPDATE-0.4.20.md)。

## 下载

在 [v0.4.20 发布页](https://github.com/HuTaoUnknow/Fukaros-Furina/releases/tag/v0.4.20) 下载并同时更新两端：

- [Fukaros：Windows x64 安装包](https://github.com/HuTaoUnknow/Fukaros-Furina/releases/download/v0.4.20/Fukaros-0.4.20-windows-x64-setup.exe)，适用于 Windows 10 / 11，包含 Qt、FFmpeg、独立 ADB 和虚拟显示驱动文件，无需预装 Qt 或模拟器。
- [Furina：Android ARM64 APK](https://github.com/HuTaoUnknow/Fukaros-Furina/releases/download/v0.4.20/Furina-0.4.20-android-arm64.apk)，适用于 Android 9 及以上，沿用原有签名，可覆盖旧版。
- 发布页同时提供 `SHA256SUMS.txt` 和 [更新说明](docs/UPDATE-0.4.20.md)。当前为稳定版本；详细测试范围见 [发布验证](docs/VALIDATION.md)。

## 功能

- **电脑 → 手机 / 平板**：复制主屏，或创建独立的 Windows 扩展桌面，支持记住扩展屏位置。
- **手机 / 平板 → 电脑**：显示 Android 画面；开启无障碍服务和远程控制后，支持点击、滑动、长按与导航按键。
- **有线与无线**：支持 USB 网络共享、USB ADB 兼容模式及局域网 Wi-Fi；同一时间只使用一个投屏方向，须由对端同意。
- **画质与方向**：1920 / 1080 / 720 / 自动档，数字为最长边像素，目标 60 FPS。竖屏扩展为 1200×1920，横屏为 1920×1200；扩展期间锁定方向，停止后恢复旋转。
- **独立鼠标**：默认开启，鼠标位置与图案通过独立认证通道传输；可切回视频合成模式。副屏鼠标速率可设为主屏的 0.25–4 倍，双端同步，主屏系统速率保持不变。
- **会话同步**：双端停止同步释放会话；停止后移除虚拟扩展屏。放大 / 还原由接收端回传状态确认，按钮跟随实际状态。
- **方向记忆**：两端分别保存分享／获取屏幕的选择，重新打开后恢复；安卓进入后台时也会保存，恢复后仍须手动开始并由对端同意。
- **日常使用**：电脑端单实例、关闭到托盘；移动端提供横竖屏布局，支持系统返回退出全屏、回到桌面后恢复投屏。调试信息按时间戳显示中英双语。

## 快速开始

1. 安装两端，在设置中填写相同的配对码（至少 6 位），移动端按开始。
2. 选择连接方式：**Wi-Fi** 需要两端处于可互访的局域网，电脑填写移动端 IPv4 地址；**USB 网络**需要先在移动端系统设置中开启“USB 网络共享”，电脑选择“USB 网络（无需调试）”，USB IP 留空可自动发现；**ADB 兼容模式**需要开启并授权 USB 调试。单纯插入数据线或仅充电不能建立投屏连接。
3. 电脑选择分享或获取屏幕，分享时再选择复制 / 扩展，按开始并由对端同意。分享 Android 屏幕还需系统录屏授权。
4. 首次扩展按提示配置 Windows 屏幕位置，之后可继承或重新配置。需要竖屏扩展时，先让移动端处于竖屏，再发起连接。
5. 投屏期间可调整清晰度和通用鼠标设置；更改方向或连接方式前先停止。安卓全屏时用系统边缘返回手势或返回键还原，普通界面返回桌面后保留会话。

设置分为 USB 和 Wi-Fi 参数页；“…”按钮提供操作步骤。电脑端关闭窗口会进入托盘，彻底退出请使用托盘菜单。

## 项目结构

```text
core/          双端共享协议、传输、会话、设置、日志和视频核心
ui/            双端共用控件、主题和全屏呈现
Fukaros/       Windows 平台实现、电脑界面与测试
Furina/        Android 平台实现、移动界面与桌面模拟测试
third_party/   FFmpeg / NVENC 头文件和许可证，只维护一份
cmake/         公共模块与构建辅助配置
tests/         公共回归测试
docs/          项目结构、更新记录、验证与第三方信息
```

可在 Qt Creator 打开仓库根目录一次构建两个桌面目标，也可分别打开两端的 `CMakeLists.txt`。Android APK 使用 `Furina/` 的 Android Kit；`.ui` 文件可在 Qt Designer 中编辑。代码使用 C11、C++17，Android 平台接口使用 Java / JNI。目录职责、依赖和开发入口见 [代码结构](docs/ARCHITECTURE.md)，历史变化见 [更新记录](docs/CHANGELOG.md)。

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

Windows 独立鼠标模式使用 WGC 采集并排除光标，兼容模式使用 DXGI；视频通过 GPU 缩放和 NVENC H.264 编码；没有可用 NVIDIA 编码器时回退至 Media Foundation。Windows 接收侧使用 FFmpeg / D3D11VA；Android 使用 MediaProjection、GPU 60 Hz 采样、MediaCodec Surface 编解码。

自动分辨率从 1080 开始，以实际编码、接收和显示帧率中的较低值评估：达到 59 FPS 及以上连续 5 秒，按 1080 → 1440 → 1920 逐档试探，1920 为目前最高档。只有低于 50 FPS 连续累计 5 秒才下降一档，恢复到 50 FPS 会清除降档计时。降档后至少观察 10 秒再尝试升档，恢复后可继续探测高档位；编码耗时或队列波动不会单独触发降档。关键帧和 QP 策略尽量降低画质突变。目标 60 FPS，实际帧率受硬件、Wi-Fi 和分辨率影响，不保证所有环境恒定 60 FPS。

局域网视频尚未使用 TLS，配对码通过 HMAC-SHA256 进行认证；只在可信局域网使用。不支持声音、剪贴板、多点触控或完整键盘输入。受保护的 Android 窗口可能黑屏。ADB PNG 预览只用于低帧率调试。

应用改名会迁移旧 `white_furina` / `black_furina` 的本地设置；连接信息和配对码不包含在仓库及安装包中。

第三方代码、驱动与运行时的来源和许可证见 [第三方组件](docs/THIRD_PARTY.md)；验证状态见 [发布验证](docs/VALIDATION.md)。
