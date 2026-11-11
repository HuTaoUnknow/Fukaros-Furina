# 平板扩展桌面

使用 VirtualDrivers/Virtual-Display-Driver 25.7.23 的官方签名 AMD64 驱动。上游 https://github.com/VirtualDrivers/Virtual-Display-Driver/releases/tag/25.7.23 ，许可证见 driver/LICENSE.txt。

virtual_display.c 使用 Windows 原生 API 创建独立 1920×1200 / 60 Hz 桌面，放在现有桌面右侧；投屏分辨率由已有自动策略单独缩放。virtual_display_controller.cpp 负责安装、等待 Qt 枚举屏幕、启停和恢复。virtual_display_helper.c 只在不可被普通用户改写的目录中执行安装和卸载：默认的按用户安装目录不可直接提权，程序会先把助手复制到 `C:\Program Files\Fukaros\virtual-display-helper\` 并核对构建时 SHA-256。驱动包仍校验固定 SHA-256 和 Authenticode 签名，不导入根证书、不关闭签名检查。细节见仓库 `docs/security-helper.md`。

首次安装需允许 Windows 提权提示。驱动设置位于上游固定路径 C:\VirtualDisplayDriver\vdd_settings.xml，已有完整设置保留；崩溃留下的半截文件会在下次安装时删掉重写。停止、断开或下次启动会用既有显示配置接口恢复虚拟屏连接前状态，普通电脑主屏保持原有设置。要移除本项目创建的显示设备，以管理员运行 `C:\Program Files\Fukaros\virtual-display-helper\virtual_display_helper.exe uninstall`；仅移除 FriendlyName 为 Furina Virtual Display 且硬件 ID 为 Root\MttVDD 的设备。按用户目录里的副本会拒绝安装和卸载。

真机已验证：必须使用 QueryDisplayConfig / SetDisplayConfig，补齐停用路径的目标扫描时序。Qt 虚拟屏名称与 GDI 设备编号分开映射。连续启停和 USB / Wi-Fi 独立桌面视频均通过，发布验证见仓库 docs/VALIDATION.md。
