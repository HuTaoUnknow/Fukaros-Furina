# 代码结构与开发入口

仓库维护两个应用，共用一份协议、传输、视频和界面组件。新增功能时，先判断它是双端能力还是平台能力，再选择下面的目录；公共代码不再复制到两端。

## 目录职责

```text
core/                           双端共享实现
  protocol/                     C 协议、视频包、清晰度策略、鼠标报文
  transport/                    TCP 连接、独立鼠标通道、设备发现
  session/                      对端同意、投屏状态、开始停止、全屏确认
  settings/                     设置迁移、鼠标偏好与双端同步
  diagnostics/                  中英日志、参数翻译、时间戳
  video/                        视频发送和接收工作线程、鼠标图案处理
    windows/                    GPU 采集、NVENC、Media Foundation、H.264 解码实现
ui/                             双端共用界面组件
  widgets/                      画面控件、设置滚动与步骤弹窗
  support/                      主题、矢量图标、方向和清晰度选择、全屏呈现
Fukaros/
  main.cpp                      Windows 应用入口、单实例启动
  core/app/                     托盘与进程实例管理
  core/display/                 虚拟显示、屏幕选择、位置记忆及驱动文件
  core/input/                   Windows 鼠标采样和副屏速率
  core/transport/               ADB 和 USB 网卡连接适配
  ui/                           电脑端界面装配与操作协调
    forms/                      Qt Designer 主界面、设置和屏幕位置表单
  tests/unit/                   Windows 专属回归测试
  tests/hardware/               需要真实 GPU / 驱动 / 平板的手动验证
  cmake/Tests.cmake             电脑端测试目标清单
Furina/
  main.cpp                      Android / 桌面模拟入口
  core/platform/                Qt 与 Android 的 JNI 桥接
  core/android/                 录屏、硬件编解码、鼠标层、前台服务和触控
  android/                      Manifest、Activity、图标和 Gradle 配置
  ui/                           移动端界面装配和横竖屏布局
    forms/                      Qt Designer 主界面与设置表单
  cmake/                        APK 配置和移动端测试清单
tests/unit/                     公共协议、传输、设置和会话测试
third_party/                    只保留一份 FFmpeg / NVENC 头文件与许可证
cmake/                          公共模块、构建默认值和测试辅助函数
docs/                           项目与发布文档
```

## 模块与依赖

| 构建模块 | 负责内容 | 主要依赖 |
| --- | --- | --- |
| `furina_protocol` | C 协议、视频策略与鼠标图案数据 | C 标准库 |
| `furina_diagnostics` | 日志格式与中英参数转换 | Qt Core |
| `furina_transport` | 认证连接、设备发现、鼠标通道 | 协议、Qt Network / Gui |
| `furina_widgets` | 共用控件、主题与全屏显示 | 协议、日志、Qt Widgets |
| `furina_session` | 同意流程、界面锁定与全屏确认 | 传输、共用控件 |
| `furina_settings` | 偏好保存与同步 | 传输、Qt Widgets |
| `furina_video` | 编解码和视频工作线程 | 协议、日志、Qt Gui、Windows 图形接口 |
| `fukaros_platform` | Windows 显示、输入、托盘及连接适配 | 公共模块、Windows API |
| `furina_android_bridge` | Android 系统调用及原生回调 | 协议、Qt Gui / JNI |

`core/session` 包含依赖界面控件的 Qt 协调器，因此不是纯 C 业务库。底层视频模块只依赖协议和日志，不再通过界面辅助头文件取得日志函数。

## 主窗口怎么读

两个应用的主窗口均按相同职责拆分：

- `mainwindow.cpp`：构造顺序、控件初始化、界面动作和尺寸变化。
- `mainwindow_settings.cpp`：设置读取、保存、展示及初始化。
- `mainwindow_session.cpp`：连接事件、开始停止、对端同意和协议动作。
- `mainwindow_video.cpp`：视频回调、帧率统计、采集与显示相关操作。

构造函数按明确的方法顺序装配各部分，保持重构前的信号注册顺序。窗口负责连接控件与服务，协议、认证、编码、显示驱动和鼠标算法仍在对应核心模块中。

共用的 `Presentation` 用 `ExitControl` 明确平台差异：电脑端保留退出全屏按钮，移动端使用系统返回；两种方式共用状态变化和全屏确认逻辑。

## 构建入口

桌面开发可直接在 Qt Creator 打开仓库根目录的 `CMakeLists.txt`，一次构建 Fukaros、Furina 桌面模拟和测试。也可以继续分别打开 `Fukaros/CMakeLists.txt` 或 `Furina/CMakeLists.txt`。需要保留整个仓库，不能只复制单个应用目录。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Android APK 请使用 Android Qt Kit 配置 `Furina/`，或运行 `Furina/build-android.ps1`。APK 的 Manifest、资源、Java 源文件汇集在 `Furina/cmake/AndroidPackage.cmake`，与公共 C++ 模块分开。

## 修改与验证约定

- C / C++ / Java 自有代码使用仓库 `.clang-format`，第三方文件不格式化、不改动。
- 不改变消息编号、设置键或 Android 包名，除非任务明确要求兼容性变更。
- 优先修改一个现有模块；只有职责独立且被多处使用时再增加模块。
- 普通测试由 CTest 运行；会改变真实屏幕布局的 `tests/hardware` 程序只供明确的手动验证，不自动执行。
- 两端界面和网络测试会使用相同测试端口，相关 CTest 用例按串行方式运行。
- 安装包、构建输出和个人 Qt Creator 状态保留在忽略目录；发布到 GitHub 前仍需用户授权。
