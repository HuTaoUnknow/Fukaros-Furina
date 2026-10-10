# Furina — Android 移动端

完整使用与构建说明见 [仓库 README](../README.md)。

独立 Qt CMake 项目；界面为 `mainwindow.ui`、`settings.ui`。Android 核心位于 `core/android/`。包标识保留 `com.furina.black`，便于覆盖旧版本。`build-android.ps1` 构建 APK；`build-desktop.ps1` 仅用于桌面模拟回归。
