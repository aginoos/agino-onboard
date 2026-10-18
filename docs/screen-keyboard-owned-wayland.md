# 屏幕键盘自带 Wayland 停靠后端（2026-10-04）

方案五界面、候选、自定义 Fn 行、Fcitx 输入和此前的收起内存优化保持。0.1.1 将 Wayland 停靠改为随 deb 安装的 Qt shell-integration 插件，不再依赖 `layer-shell-qt` 或 `liblayershellqtinterface6`。

## 实现

插件 key 为 `agino-keyboard`，安装于 Qt6 的 `wayland-shell-integration` 目录。协议使用 bottom 锚定、top 层、无键盘焦点，按窗口逻辑高度预留空间。首次无缓冲提交后等待 configure，确认序号并调整尺寸，才允许 Qt 绘制；零尺寸配置保留客户端请求尺寸。收起释放角色和占位，持续收起仍按既有 3 秒策略释放窗口、QML、GUI 字体缓存及空闲堆页。

屏幕切换显式重建角色，因为同一虚拟桌面的 QWindow::setScreen 不会重建绑定旧输出的 layer surface。合成器 closed 事件的恢复回调绑定旧角色生命周期，角色被替换后不会再次重建新角色。

只有键盘 Wayland 进程选用插件；设置进程清除该选择，使用普通 xdg 窗口。插件缺失时 Qt 拒绝启动，不回退到普通键盘窗口。X11 dock/strut 路径保持。

插件核心手写 157 行，另有共享属性、构建和回归测试；协议描述与包装由扫描器生成。生成器对 XML 中 namespace 参数改名 scope，仅改 C++ 参数名，不改变线上协议。wlr 和 xdg XML 的上游版权随安装包保存。

使用 Qt6.8.0 私有接口；deb 明确约束 `qt6-base-private-abi (= 6.8.0)` 与 `qt6-waylandclient-private-abi (= 6.8.0)`，升级 Qt 后需重新编译验证。协议与 Qt 生命周期依据：[wlr layer-shell](https://github.com/swaywm/wlr-protocols/blob/master/unstable/wlr-layer-shell-unstable-v1.xml)、[Qt6.8 QWaylandWindow](https://github.com/qt/qtwayland/blob/v6.8.0/src/client/qwaylandwindow.cpp)、[Qt6.8 QWindow](https://github.com/qt/qtbase/blob/v6.8.0/src/gui/kernel/qwindow.cpp)。

## 最终安装包

- 源码提交：`8146d98abe513af77b1ef42dda1adf39fe0c812d`；源码归档的 55 个文件逐个匹配提交。
- `agino-keyboard_0.1.1_amd64.deb`：415,296 字节，约 406 KiB；Installed-Size 2,088 KiB，约 2.04 MiB。此值不含系统已有依赖。
- SHA256：`6758bd5ff669f6cf2d6977412718279af8eb3f935bd98d3a0e1eaf68dcdc0756`。
- 实际 deb 的 Depends 与 6 个 ELF 文件均无 LayerShellQt 依赖，包含自有插件。本机 apt 安装模拟只新增应用本身，0 个额外依赖包。
- 匹配 Deepin25 / amd64、Qt6.8.0、DTK6 Declarative6.7.47.2、Fcitx5 5.1.12-2deepin12、KWin4:6.1.38。

## 验证

- 隔离 SDK 和干净构建中不含 LayerShellQt。最终干净构建 **18/18 CTest**，0 失败；原生 Wayland、文本输入 v3、XWayland、重启恢复、输入、安全输入、窗口重建、两屏切换和协议生命周期均通过。仓库 **16/16 unittest**。
- 实际嵌套 KWin：最大化输入目标给键盘让位，Fn 改变占位高度，收起还原目标区域，持续收起重建保留焦点和输入；设置使用 xdg 普通窗口。线上的协议跟踪确认无焦点、bottom/top、先 ack 再提交缓冲。
- 独立审查发现两屏移动及旧关闭回调问题，均先用真实运行的用例复现，再修复。双屏用例从目标高度456未恢复800变为通过；受控协议 peer 使用实际插件，从3个角色变为预期2个，并覆盖零尺寸 configure。
- 最终 deb 升级安装到自有完整 DDE X11 虚拟机，**14 通过、0 失败、10 项按环境跳过**；跳过项在隔离原生 Wayland/私有会话中另行运行。
- 11 张安装包截图与上一包像素完全相同：候选、展开、浅/深色、设置、Fn，以及100%/150%/200%缩放。全部 QML 文件和输入桥接与此前提交相同。

三轮全新 GUI 进程，在相同自有 Deepin25 DDE X11 KVM（4GiB、2vCPU、1920×1080）读取 smaps 元数据。单位 MiB；不包含已有 Fcitx 或桌面进程。

| 状态 | RSS 中位数 | PSS 中位数 | USS 中位数 |
| --- | ---: | ---: | ---: |
| 隐藏启动 | 55.59 | 12.55 | 7.40 |
| 显示 | 91.43 | 37.11 | 25.17 |
| 输入中 | 96.14 | 40.43 | 27.71 |
| 输入后收起5秒 | 75.57 | 23.61 | 13.48 |
| 展开后收起5秒 | 76.12 | 24.08 | 13.98 |

上一包收起后 PSS 为23.62 MiB，本次23.61 MiB；输入40.19→40.43 MiB。这些是不同批次样本，未证明输入时进一步降低内存。保留此前“收起后约40%”优化：相对上一轮40.09 MiB优化基线，本次首次收起约下降41%；反复重建后的末轮收起为24.10–24.29 MiB，约下降39–40%，不保证每次严格40%。以前的实验见 [收起优化报告](screen-keyboard-idle-optimization.md)。

持续收起后12次重建，Qt 首帧提交中位数 **79.3 ms**，范围 **68.0–125.8 ms**；DBus调用返回中位数54.7 ms。6次快速呼出首帧中位数4.6 ms。指标为 Qt 帧提交，不能当成实际屏幕显示时间或所有设备的保证。

原始证据保存在 `out/components/agino-keyboard-validation/owned-wayland/`，交付包内位于 `validation/owned-wayland/`。旧安装包与证据保存在 `out/components/agino-keyboard-before-owned-wayland/`。

测试过程中的 KWin 崩溃经 backtrace 定位为最大化动画在 QPainter 后端调用 GLTexture，测试会话关闭该特效后再验证。首次开发测试有一项因运行中编辑包装脚本而语法失败；旧日志保留，固定源码后的最终18项全部通过。

完整 DDE Wayland、真实触屏/笔、旋转、多屏物理热插拔和休眠仍需要设备验收。新的整机冷启动内存门槛未重测，前一版本整机门槛失败。`release_ready=false`，交付为本机测试预览。
