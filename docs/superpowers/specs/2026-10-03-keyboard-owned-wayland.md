# 屏幕键盘自带 Wayland 停靠后端

用户已确认：保留方案五 DTK/QML 外观和现有 Fcitx 输入、安全输入、候选及收起重建行为，自行实现最小 Qt Wayland 窗口集成，去掉 layer-shell-qt 与 liblayershellqtinterface6 的安装依赖。沿用已有独立 worktree，直接执行并交付新 deb。

实现为随应用安装的 Qt Wayland shell-integration 插件，key 为 agino-keyboard。只有键盘 Wayland 进程选用此插件；设置进程仍使用普通窗口。依赖现有 Qt6 WaylandClient、libwayland-client，不替换 Qt 渲染、合成器、输入法或输入协议。底部锚定、top 层、无键盘焦点、exclusive zone、configure/ack/初次映射、销毁和重建由插件实现。Qt 6.8.0 私有接口 ABI 必须随包声明并在升级时重新验证。协议 XML 及其许可证随源码归档。

X11 保留已有 dock/strut 实现。拒绝缺少 layer-shell 的 Wayland 环境，不能无声降级到普通窗口。不强制卸载用户已有的 LayerShellQt 包，只证明本程序不需要它们。

验收：旧包依赖检查必须失败，新包无这两个 Depends/ELF 引用且包含自己的插件；实际嵌套 KWin 上目标保留焦点、最大化目标给键盘让位、收起恢复、Fn 高度与窗口重建更新占位；既有输入/候选/安全/恢复全套通过。当前完整 DDE X11 VM 和三轮 PSS/首帧、方案五像素对照复测。完整 DDE Wayland 和真实触屏矩阵仍未验证，release_ready=false。
