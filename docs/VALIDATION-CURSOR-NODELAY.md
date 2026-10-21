# 独立鼠标映射专项检查（2026-10-21）

上一轮60FPS与缩放A/B测试主要验证视频链路，未测物理鼠标到平板光标显示的真实端到端延迟。

本轮发现并修复：CursorTransport客户端在连接前设置LowDelayOption，实际连通后读取为0。Qt原生socket尚未创建时设置选项不能保证生效。现在在connected()回调和已认证ready()阶段设置，覆盖初次连接和重连；接收端也保证为1。

- 修改前专项测试：cursorConnectionAuthenticatesAndCoalesces失败，实际LowDelayOption=0，期望1。
- 修改后该测试及cursorConnectionIsIndependentOfVideo通过，包含认证、合并最新坐标、两侧低延迟选项、视频通道阻塞期间鼠标仍可传递。
- 证据：任务work/cursor-before-nodelay.txt、work/cursor-after-nodelay.txt。
- 这证明配置缺陷已修复，不能直接换算为真实端到端延迟降低多少，也不能证明所有鼠标卡顿已经消失。8毫秒采样仍由电脑Qt事件线程驱动，安卓显示仍需等待帧回调；其繁忙时的抖动尚未专项实测。

修复源码已保存到正式项目，双端本地 0.4.20 修订包的签名 / 对齐校验通过。后续两端已覆盖安装鼠标修复；移动端当前是含修复的临时观测构建，观测开关已关闭。用户随后要求停止测试，临时 USB 转发已撤销。

2026-10-21 源码同步包含上述低延迟修复，临时观测代码保留在本地。本次同步检查文件、版本和已有验证记录，没有重新运行实机测试，也未发布 0.4.20 安装包。

[Qt官方socket选项说明](https://doc.qt.io/qt-6/qabstractsocket.html#setSocketOption)：选项在原生socket创建后才生效；连接完成后设置可保证时机正确。
