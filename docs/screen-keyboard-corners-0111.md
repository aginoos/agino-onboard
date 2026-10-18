# 屏幕键盘 0.1.11：无 X11 合成时的圆角黑框

2026-10-04。基线 0.1.10（e7c9cd7）。原始构建、测试、截图与包记录位于 `out/components/agino-keyboard-validation/corners-0111/`。

## 原因与修复

用户截图显示圆角外的四个角为黑色，整个窗口仍为矩形。只读检查确认当前桌面为 X11、键盘使用 dxcb/xcb、KWin 的 Compositing.active=false，当前安装版本为 0.1.9。没有修改主机合成设置、安装包或重启用户会话。

QML 的 12 逻辑像素圆角与 QQuickView 的透明清屏颜色本身正确；旧测试 grabWindow 只检查该窗口渲染图像的 alpha，不能说明窗口被实际桌面合成后的结果。新增隔离 Xvfb 桌面捕获测试在旧代码上得到黑色四角，而背景应为绿色，复现了用户现象。

在 X11 合成关闭时，PanelView 按根面板 radius 和当前尺寸提交 QWindow 原生窗口遮罩，把圆角外侧排除出窗口。合成开启时清除遮罩，由透明 alpha 保留抗锯齿边缘；使用已有 DTK DWindowManagerHelper 状态与通知，不增加运行依赖或定时轮询。源加载、尺寸、显示和屏幕变化均刷新遮罩。Wayland 保留原透明 surface 路径，不进入 X11 遮罩分支。

窗口 DPR 变化时，即便逻辑区域未变也强制重交遮罩，因为 Qt 6.8 的 setMask 在调用时换算为原生像素。事件先交给 QQuickView，再更新裁切，保留其原有渲染处理。拖动、停靠、输入焦点、候选词与设置布局保持 0.1.10 行为。

相关实现参考：

- [DTK DWindowManagerHelper](https://github.com/linuxdeepin/dtkgui/blob/master/src/kernel/dwindowmanagerhelper.cpp)
- [Deepin DXcb 合成状态通知](https://github.com/linuxdeepin/qt5dxcb-plugin/blob/master/xcb/dxcbwmsupport.cpp)
- [Qt 6.8 QWindow 原生 mask / DPR 处理](https://github.com/qt/qtbase/blob/v6.8.0/src/gui/kernel/qwindow.cpp)

## 验证

- 桌面截图测试先在旧实现失败（四角为黑色），修复后无合成环境的 100%/200% 桌面显示检查通过；覆盖 828×344 与 934×400、隐藏再显示。
- 通过私有 Xvfb 上的真实 Deepin KWin root 属性 `_NET_KDE_COMPOSITE_TOGGLING` 通知 DTK，验证合成开启清空遮罩，关闭恢复圆角遮罩。这是通知协议测试，不把假属性当作完整合成器渲染测试。
- Qt 私有测试接口驱动存活窗口的 DPR 变化，隔离尺寸通知，用 XShapeGetRectangles 读取真实原生形状，检查 2×→1.5×→1× 后的独立已知边缘点。删除 DPR 事件处理时测试真实失败，保留处理时 100%/200% 的 native tests 通过。查询前 Qt sync，避免跨 X 连接未 flush 的伪失败。
- 最终完整组件回归 33/33 通过（297.05 秒），见随包 ctest-final.log；仓库 unittest 16/16 通过。独立代码审查指出的 DPR 问题已修正并通过复审。
- 最终 GUI 在匹配 deepin 25 的 Qt/DTK SDK、私有 Fcitx/Xvfb、关闭合成状态生成四张实际桌面截图（空候选/设置，各 100%/200%）。窗口保持 934×344 逻辑像素，四角均显示桌面背景 RGB(185,211,227)。截图带 16 逻辑像素背景边距，100% 为 966×376，200% 为 1932×752。并非窗口内部 alpha 截图。
- 截图显式使用 heightPercent=25，以展示最小面板；产品默认高度仍为 40%。GUI 截图使用同一构建的未 strip 二进制，交付 deb 为该构建 strip。

## 尺寸与交付

默认关闭 Fn 的布局下限为 828×344 逻辑像素；开启 Fn 后下限高度 400。200% 下分别对应 1656×688 和 1656×800 物理像素。宽度目前根据屏幕自动取 828～934，尚无手动宽度滑块；高度滑块最低为逻辑屏幕高度的 25%，实际不能低于布局下限。

0.1.11 安装包为 443172 字节，Installed-Size 2176 KiB。相比 0.1.10，仅 GUI、README 和插件版本配置变化，其余 18 个载荷文件不变；Depends 不变，重新计算 shlibs 一致，六个 ELF 无 LayerShellQt 依赖。主机安装模拟仅从 0.1.9 升级 agino-keyboard，新增包数为 0。0.1.11 包含 0.1.10 的候选行设置入口、加长左 Shift 与完整设置覆盖层。

应用源码、完整仓库快照、匹配 Fcitx/KWin 上游源码、补丁和 GPL 通知一起提供；未分发搜狗二进制。未在本轮验收完整 DDE Wayland、真实触屏/笔、实际多屏热插拔/缩放、旋转、休眠或通用 xcb 上的实时合成通知。隔离协议与 Qt DPR 测试不能替代整机设备验收，release_ready=false，此包用于本机测试。
