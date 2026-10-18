# Deepin 搜狗启动兼容修复（0.1.2）

## 问题与根因

用户在 Deepin 25 上安装 0.1.1 后，执行 `agino-keyboard --hidden` 弹出“已有屏幕键盘占用 Fcitx5 界面”，并打印缩放策略初始化顺序警告。

本机只读检查确认没有其他 Agino 键盘。`org.fcitx.Fcitx5.VirtualKeyboard` 与 `org.fcitx.Fcitx5` 由同一个连接 `:1.2075`、同一个 fcitx5 进程提供。该进程加载的 `/opt/apps/com.cpis/lib/x86_64-linux-gnu/cpis-module-im-fcitx5.so` 注册了这一接口，当前输入法为 `com.sogou.ime.ng.fcitx5.deepin`。这是搜狗 CPIS 内置接口，被旧版误判为独立屏幕键盘。系统 Fcitx 虚拟键盘配置文件原本不存在。

Qt 6.8 的 `QHighDpiScaling::initHighDpiScaling()` 在 QApplication 构造期间读取 `QT_SCALE_FACTOR_ROUNDING_POLICY`，并调用此时已经过晚的公开 setter。GDB 回溯与对应 Qt 源码确认这一调用路径；本机环境值为 `PassThrough`。

## 修改

- 注册虚拟键盘服务失败时，仅在它与 Fcitx 主服务的有效、非空唯一连接相同时允许继续。保留既有接口，本应用通过独立的 `org.agino.Keyboard1` 桥接输入。独立屏幕键盘的占用仍然拒绝，不抢占服务。
- 只有本应用实际取得虚拟键盘接口时，才设置 EnableVirtualKeyboard 并请求 ReloadConfig；兼容 CPIS 时不改配置、不重载输入法。
- 在构造 QApplication 前按 Qt 的五种策略和大小写规则应用有效缩放策略，构造期间暂时移除该环境变量，随后恢复原值，保证设置窗口等子进程继续继承。无效值保留 Qt 原有诊断。
- 方案五 QML、输入桥接、收起内存回收和自有 Wayland 插件均未改动。0.1.2 继续不依赖 LayerShellQt。

## 回归验证

证据目录：`out/components/agino-keyboard-validation/startup-compatibility/`。

修复前两个真实程序启动回归均失败（`startup-red.log`）：后端已占接口时无法提供 `/Keyboard`，以及出现过晚设置缩放策略警告。修复后新增测试 11 个用例通过（`startup-green.log`），覆盖五项有效策略、混合大小写、无效策略诊断、环境恢复、后端所有权及配置字节保持。原有独立占用拒绝测试继续覆盖第三方界面。

本机隐藏启动验证（`native-startup.json`）使用新二进制与隔离应用配置，不发送按键、不改变焦点、不重启 Fcitx。程序正常注册 `/Keyboard`，visible/viewCreated 均 false；无缩放警告。原服务所有者、当前输入法及系统虚拟键盘配置的存在状态保持不变。

首次开发 SDK 的完整 19 项测试中，18 项通过，XWayland 套件两项输入断言失败。保留 `sdk-tests.log` 与 `xwayland-initial-kwin.log`；未改动输入代码，原失败套件完整重跑通过（`xwayland-repeat.log`，36.55 秒）。尚未确定首次失败的原因，不将重跑称为已修复该失败。最终干净构建与安装包检查另有日志记录。

本次没有重新采样内存；此前内存和首帧数据仍归属于 0.1.1 的[自有 Wayland 报告](screen-keyboard-owned-wayland.md)。本次启动检查不等于搜狗候选词和真实触屏设备的完整验收。

## 最终安装包

应用源提交：`96c401afa0c9e490f1ec89f01894dcca75fd9f20`；57 个归档文件与该提交逐项一致。

- 干净构建：19/19 CTest，0 失败，包括 XWayland（32.39 秒）和新增启动测试。全程 232.22 秒。仓库 unittest 16/16 通过。
- 最终 deb 安装至独立完整 DDE X11：14 项输入测试通过，10 项按私有会话或其他环境条件跳过。
- 100% / 150% / 200% 缩放、深浅色、Fn、设置与候选界面共 11 张截图，与 0.1.1 逐像素相同；截图程序日志没有过晚设置缩放策略警告。
- 从最终 deb 解包的程序，在本机搜狗环境重新通过隐藏启动保护性检查，原接口所有者、输入法及配置保持不变（`native-package-startup.json`）。
- 本机 apt 安装模拟：仅升级 agino-keyboard，新增依赖 0 个。6 个 ELF 无 LayerShellQt 依赖，保留 Qt 6.8 私有 ABI 约束。

`agino-keyboard_0.1.2_amd64.deb` 为 415,844 字节，Installed-Size 为 2,092 KiB。
SHA256：`1f80de1234f0a7cbfb09e730139cc107a3296b6254fc062529fe9c4a1b8e5fb3`。
