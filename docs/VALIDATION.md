# v0.4.20 发布验证

日期：2026-10-23。Fukaros 与 Furina 均为 **0.4.20**，Android versionCode **24**。

## 正式构建

- 使用正式源码构建 Windows x64 Release 和 Android ARM64 Release，临时鼠标观测代码保留在原本的本地构建工作区，不进入发布源码或安装包。
- Windows 使用 Qt 6.11.1 / MinGW 13.1；安装包包含 Qt、FFmpeg、独立 ADB、虚拟显示驱动及许可文件。
- Android 使用 Qt 6.11.1 / NDK r27c / JDK 21，最低 API 28，目标 API 35；沿用既有签名证书，校验签名及 16 KiB ZIP 对齐。
- 仓库标签、主分支提交和正式构建源码一致，发布附件以 SHA-256 核验；本地覆盖安装使用相同的发布安装包。

## 本轮回归

五组 CTest 回归通过：`protocol`、`peer`、`ui`、`black_ui`、`screen_scale`。界面测试使用 offscreen，连接测试使用本机回环，不连接实际平板或创建真实投屏会话。

方向记忆覆盖分享／获取两个方向的窗口销毁与重建、模式区域显示、异常记录回退、取消设置不改变方向，以及安卓后台保存不接受连接设置中的未保存编辑。恢复后保持未开始、未授权的状态。

## 历史实机结果与边界

- 2026-10-21 的缩放 A/B 对照及解码恢复数据见 [缩放记录](VALIDATION-SCALE-TOGGLE.md)。
- 2026-10-21 的独立鼠标 socket 低延迟设置检查见 [鼠标记录](VALIDATION-CURSOR-NODELAY.md)。该检查证明配置生效，不等同于物理鼠标到显示的端到端延迟测量。
- v0.4.19 的完整 20 组回归和原生画质检查保留在 [历史发布验证](https://github.com/HuTaoUnknow/Fukaros-Furina/blob/v0.4.19/docs/VALIDATION.md)。本轮没有将这些历史结果计作重新运行通过。

按既有停止测试要求，本次未重新执行实机投屏、虚拟屏布局或性能压测。安装与文件一致性核验不替代完整 USB／Wi-Fi 真机验证，也不承诺全场景恒定 60 FPS。
