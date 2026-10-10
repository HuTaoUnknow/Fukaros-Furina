# 0.4.0 发布验证

桌面端正式名称为 Fukaros，移动端正式名称为 Furina。两端在同一个仓库中分别保留独立 Qt CMake 项目，核心代码位于各自的 `core/`。

## 构建与回归

- Fukaros：Qt 6.11.1 / MinGW 13.1，Release 构建成功。
- Fukaros CTest：9 / 9 通过，包括协议、显示源、网络对端、投屏确认、界面、多屏界面、视频策略、虚拟显示驱动包及鼠标合成。
- Furina 桌面模拟 CTest：3 / 3 通过，包括移动端界面、视频策略及鼠标合成。
- Furina Android：Qt 6.11.1 ARM64 / NDK r27c / JDK 21，Release APK 构建成功。
- 所有公开 PowerShell 构建脚本通过语法解析；发布源码扫描未发现个人设备信息或 GitHub 凭据。

## Windows 安装包

`Fukaros-0.4.0-windows-x64-setup.exe` 由 Inno Setup 7.1.0 构建，按当前用户安装；包含 Qt、FFmpeg 7.1.3、ADB 及签名的虚拟显示驱动文件。

实际安装至临时测试目录，通过安装成功、依赖文件完整、去除 Qt / MinGW 开发路径后启动主窗口、正常关闭、内置 ADB 启动和卸载检查。测试副本已卸载。

首次使用扩展功能仍由应用单独请求驱动安装权限。

## Android 安装包

`Furina-0.4.0-android-arm64.apk`：

- 应用标签：Furina。
- 包标识：`com.furina.black`，保留以便覆盖旧应用。
- versionName：0.4.0；versionCode：4。
- 最低 API 28 / Android 9，目标 API 35，编译 API 36。
- 架构：arm64-v8a；包含 `libFurina_arm64-v8a.so`。
- Release 配置，APK 没有 debuggable 标志。
- APK 签名校验与 16 KB zipalign 检查通过；证书与旧预览 APK 一致。
- 第三方许可证包含在 `assets/licenses/` 中。

预览 APK 使用沿用的 Android 测试签名；自行构建正式生产版本时使用自己的签名密钥。密钥不包含在公开仓库。

## 验证范围

本轮对 Android APK 验证构建、资源、标签、架构、签名和对齐；真机应用保持用户当前运行版本。发布保留已实现的复制 / 扩展、鼠标显示、布局变化恢复、画质稳定、全屏边框修复、方向互斥和对端同意流程。

视频目标为 60 FPS，实际结果受硬件、分辨率和网络影响。Furina 桌面模拟测试验证界面和协议，实际投屏使用 Android 系统采集与硬件编解码。安装包校验值随 Releases 提供。
