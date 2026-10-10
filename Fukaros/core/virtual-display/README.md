# 平板扩展桌面

使用 VirtualDrivers/Virtual-Display-Driver 25.7.23 的官方签名 AMD64 驱动。上游 https://github.com/VirtualDrivers/Virtual-Display-Driver/releases/tag/25.7.23 ，许可证见 driver/LICENSE.txt。

virtual_display.c 使用 Windows 原生 API 创建独立 1920×1200 / 60 Hz 桌面，放在现有桌面右侧；投屏分辨率由已有自动策略单独缩放。virtual_display_controller.cpp 负责安装、等待 Qt 枚举屏幕、启停和恢复；virtual_display_helper.c 独立提权安装，校验固定 SHA-256 和 Authenticode 签名，不导入根证书、不关闭签名检查。

首次安装需允许 Windows 管理员/发布者提示。驱动设置位于上游固定路径 C:\VirtualDisplayDriver\vdd_settings.xml，已有设置保留。停止/断开会恢复虚拟屏连接前状态，普通电脑主屏保持原有设置。要移除本项目创建的显示设备，以管理员运行 bin/virtual_display_helper.exe uninstall；仅移除 FriendlyName 为 Furina Virtual Display 且硬件 ID 为 Root\MttVDD 的设备。

真机已验证：必须使用 QueryDisplayConfig / SetDisplayConfig，补齐停用路径的目标扫描时序。Qt 虚拟屏名称与 GDI 设备编号分开映射。连续启停和 USB / Wi-Fi 独立桌面视频均通过，发布验证见仓库 docs/VALIDATION.md。
