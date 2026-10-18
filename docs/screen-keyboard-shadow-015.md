# 屏幕键盘 0.1.15：Deepin X11 窗口阴影

日期：2026-10-05。基线：052c6f6（0.1.14）。

透明、无边框的屏幕键盘此前只在 QML 中绘制圆角。匹配的 Deepin KWin 6.1.38 会跳过缺少原生圆角声明的 alpha 窗口阴影（windowstyle/windowshadow.cpp 的 updateWindowShadow）。

PanelView 在 DXcb 平台且 DWindowManagerHelper.hasNoTitlebar 为真时创建 DPlatformHandle，使用 KWin 管理的无标题栏外观及系统默认阴影。原生半径跟随 QML 标准 12 / 紧凑 8；borderWidth 为 0 避免重复 QML 边框；禁用 DTK 系统拖动/缩放，保留已有拖动与 dock/no-focus 属性。其他 X11 平台保留原有圆角回退，未开启 DTK 父窗口回退。Wayland layer-shell 阴影本版未改动，也未声称修复。

原生刷新连接使用 compositorWatcher 的生存期；成员对象销毁后，QWindow 析构时的 visibleChanged 不再访问已销毁的外观状态。

隔离验证：

- 修复前，私有 kwin_x11 的原生圆角声明测试失败（0 而非 12）。
- 修复后，真实停靠/不接受焦点窗口的外部阴影像素、尺寸、12→8 半径切换、隐藏/重显在 1×/2× 通过。
- 8 项 QML、资源、原生圆角/阴影 CTest 通过；另 6 项 Fcitx、桌面、快捷键、候选所有权和 Wayland shell 生命周期 CTest 通过。未重跑全部测试。
- 仓库 16 项单元测试通过。独立只读代码审查无可操作问题。
- 发布安装包的实际 GUI 在私有 Xvfb / DBus / Fcitx / Deepin KWin 会话捕获标准/紧凑 100%/200% 和设置、中文候选界面。未操作宿主桌面或读取用户输入。

证据目录：out/components/agino-keyboard-validation/shadow-015。截图来自 stage-shadow-015 中实际安装包的 GUI；随后 README 更新，包中文档重新生成，执行文件保持一致。

不增加新的运行库依赖，不修改宿主桌面特效设置；需退出旧键盘并启动新版本。X11 桌面关闭合成时不能显示合成器阴影。
