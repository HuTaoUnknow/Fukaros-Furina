# 虚拟显示提权助手与 Linux CI

电脑端默认安装在 `%LOCALAPPDATA%\Programs\Fukaros`，该目录对当前用户可写。提权安装虚拟显示驱动时，不能直接对这个目录里的 `virtual_display_helper.exe` 使用 `runas`：同权限进程可以在用户确认之前换掉助手。

## 启动规则

构建 `virtual_display_helper` 之后，CMake 计算它的 SHA-256，写入生成头 `FR_VIRTUAL_DISPLAY_HELPER_SHA256`，并编进 `Fukaros`。每次准备安装都会重新计算磁盘文件的哈希。

`fr_helper_launch_decide` 只允许下面两种提权目标：

- 助手已经在当前用户的非提权令牌不能修改的目录中，并且哈希与构建指纹一致。
- `Program Files\Fukaros\virtual-display-helper\virtual_display_helper.exe` 已存在，目录不可被该令牌修改，且哈希一致。源文件被替换时仍使用这份受保护副本。

源文件位于可写目录时，先复制到上述受保护目录，写上仅 SYSTEM 与 Administrators 可写、Users 只读执行的 DACL，核对副本哈希，再启动副本。进程尚未提权时，提权的是 `%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe`（32 位进程则用 `Sysnative`）。脚本在提权后再次核对哈希，设置 DACL，然后才启动受保护副本。不会从可写目录提权助手本身。

助手执行 `install` 或 `uninstall` 时，若自身映像或其目录对非提权令牌可写，直接退出码 15，不改驱动。`verify` 只读校验驱动包，仍可在构建目录运行，供现有包测试使用。

手动卸载请在助手已被放到受保护目录之后，以管理员运行：

```text
C:\Program Files\Fukaros\virtual-display-helper\virtual_display_helper.exe uninstall
```

## 崩溃残留

下一次启动或下一次安装会清理这些残留：

- 记录在 `%TEMP%\Fukaros-vdh.pid` 的已退出安装进程，以及 `Fukaros-vdh-*` 临时文件。仍在运行的安装不会被第二次启动。
- `%ProgramData%\Fukaros\virtual-display-helper\helper.pid` 指向的、父进程已退出且映像位于不可写目录的助手进程。确认映像名与目录后再结束它。
- `C:\VirtualDisplayDriver\vdd_settings.xml` 仅在它是随附模板的截断前缀或空文件时删除并重写。已有完整设置（含 `</vdd_settings>`）保留。
- 半应用的虚拟屏拓扑使用既有的 `fr_virtual_detach_idle`。控制器构造时和受保护安装开始时都会调用它。不自动卸载已经登记的 `Root\MttVDD` 设备；需要重启的安装（退出码 3）保持原行为。

## Linux CI

`.github/workflows/linux-unit-tests.yml` 在 Ubuntu 上配置仓库根目录的 CMake，并运行 `ctest`。其中包含 `tests/unit` 的可移植用例，以及 `helper_launch`（哈希、提权决策、残留判定、设置文件截断）。不构建 Android APK，不运行 NVENC，不安装 IDD 驱动。

仓库里没有不依赖模拟器的 Android JVM 测试。Furina 现有的桌面 Qt 测试随这次 `ctest` 在宿主机运行，不启动模拟器。
