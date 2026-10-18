# Agino 屏幕键盘实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 交付遵循 Deepin DTK 风格、与 Fcitx5 集成的桌面/二合一屏幕键盘，并验证 X11、触屏与 KWin Wayland 输入。

**Architecture:** C++ / Qt6 控制器驱动 QML/DTK6 界面，Fcitx5 虚拟键盘模块提供预编辑和候选更新。
会话 addon 为焦点、敏感用途和候选代次提供权威校验；桌面适配器分别负责 X11/XI2 和 KWin Wayland/LayerShellQt。
只运行普通用户进程，保留独立构建包和隔离验证产物。

**Tech Stack:** C++17、Qt6 Core/Gui/Quick/Qml/DBus/Test/WaylandClient、DTK6 Gui/Declarative、Fcitx5 SDK、Xlib/XI2/XTest、LayerShellQt、CMake、Debian 打包。

**Spec:** [架构与布局五稿已确认](../specs/2026-10-03-screen-keyboard-design.md)。

## Global Constraints

- 首版主要面向桌面与二合一触屏设备；首版范围为登录后的普通用户会话。
- 普通用户会话运行，不增加系统级键盘注入服务。
- 按键最小触控区域为 48 个逻辑像素；默认高度目标为可用屏幕高度的 40%，必要时以完整布局和最小触控尺寸为先。
- 默认隐藏功能行，设置中允许开启 F1–F12；数字行默认显示，隐藏功能行时 Esc 位于数字行首、Del 位于反斜杠之后。
- 顶部为窄标题行：设置图标最左、屏幕键盘标题居中、关闭按钮最右；无自动下拉，底行无设置/关闭键。
- ↑ 在 ↓ 正上方，方向键为倒 T 字布局。
- 相较三稿收窄一列普通键宽；箭头组左移一格，中/英移至 ↑ 右侧；保持右 Shift 和普通键尺寸。
- 候选条支持横滑、仅有一个向下展开箭头；展开网格覆盖按键区，选词或再次点击箭头还原，不改变面板几何。
- 退格在持续按住 450 ms 后以约 60 ms 间隔重复。
- 单次按键只选一种路径，避免重复输入；没有可编辑焦点时停止发键。
- 不保存预编辑、候选或用户按键日志；故障恢复时不重放断开期间的旧按键。
- 系统空闲内存上限 1,000,000,000 字节，按三次冷启动、120 秒稳定期和 60 秒采样验证。
- 实际使用 DTK6 控件和系统主题，触屏按键不照搬普通按钮的默认尺寸。
- 保留工作区其他开发改动；不覆盖已有构建或 VM 原始镜像，不安装宿主系统软件包。
- 普通窗口显示成功不算 Wayland 输入成功；所有未验证项目明确记录，不宣称全硬件兼容。

## Review Focus

1. 快速切换应用、候选更新与点击交错：旧候选不能落入新目标（任务 2、3）。
2. 多指、触点取消和鼠标合成事件交错：每次按下只产生对应的一组释放，不能卡键（任务 4）。
3. 后端所有者更换、锁定和唤醒：清空敏感状态，不重放旧输入（任务 2、3、5）。
4. 缺少文本协议/设备来源/窗口角色：能力不可伪装为可用，可靠手动模式须可用（任务 1、5）。
5. 缩放、旋转、显示器拔插与主题切换同时发生：可点击尺寸、系统配色与非聚焦行为仍正确（任务 4、5）。

## 文件与责任

`apps/agino-keyboard/` 新增：

- `CMakeLists.txt`、`README.md`、`COPYING`、`data/`：构建、许可、桌面/托盘入口和会话配置。
- `src/main.cpp`：启动参数、单实例、QML/DTK初始化与托盘。
- `src/keyboardstate.{h,cpp}`：显示策略、触点、按键配对、重复输入与候选代次。
- `src/fcitxclient.{h,cpp}`、`src/virtualkeyboardui.{h,cpp}`：异步会话协议与 Fcitx5 UI 回调。
- `addon/focusbridge.{h,cpp}`、`addon/agino-keyboard.conf.in`：Fcitx5 焦点/用途及发送时校验。
- `src/desktopadapter.{h,cpp}`、`src/x11adapter.{h,cpp}`、`src/waylandadapter.{h,cpp}`：能力、设备来源、屏幕、窗口与兼容发键。
- `qml/KeyboardPanel.qml`、`KeyTile.qml`、`CandidateBar.qml`、`SettingsWindow.qml`：DTK主题界面及触控。
- `tests/`：QtTest 状态机、私有 D-Bus/Fcitx5 集成、QML与窗口检查。

仓库新增 `scripts/build-screen-keyboard.sh`、`scripts/guest/build-screen-keyboard.sh`、
`scripts/verify-screen-keyboard.py` 和 `docs/screen-keyboard-validation.md`。
集成修改 `scripts/build-prototype.sh`、`scripts/guest/configure.sh`、`config/packages/desktop.list`、
`README.md`，实施时按当前内容进行最小修改。

### Task 1: 确认运行接口与隔离构建输入

**Files:** `scripts/guest/build-screen-keyboard.sh`；`apps/agino-keyboard/tests/protocol-contract.xml`；`docs/screen-keyboard-validation.md`。

**Interfaces:** Produces 安装包实测 D-Bus 签名、SDK CMake target 与窗口角色能力记录。

- [ ] 在项目专用容器核实 DTK6 Declarative、Fcitx5 SDK 和 LayerShellQt 的头文件、CMake target 与运行依赖。
  当前缓存候选分别为 `6.7.47.2`、`5.1.12-2deepin12`、`6.3.4-1`；记录实际安装版本，不把缓存当最终锁定。
- [ ] 私有 D-Bus 会话启动安装版 Fcitx5，实时 introspection `/virtualkeyboard`；保存匹配安装版的协议契约。
- [ ] 验证安装版 KWin 可在嵌套 Wayland 会话启动，并能授予 LayerShellQt 非聚焦窗口角色；记录失败原因。
- [ ] 能力验证断言：缺少文本协议或 layer-shell 时 `waylandReady == false`，不能静默回落普通窗口并标记成功。
- [ ] 将上述日志、版本与已确认接口写入验证报告，再继续依赖这些接口的实现。

### Task 2: 按键与显示状态机

**Files:** `src/keyboardstate.{h,cpp}`、`tests/test_keyboardstate.cpp`、`CMakeLists.txt`。

**Interfaces:** Produces `KeyboardState::beginTouch(int, KeySpec)`、`endTouch(int, bool)`、
`cancelAll()`、`setContext(quint64, bool, bool)`、`setMode(Mode)`、`setSource(InputSource)`；
signals `keyRequested(KeySpec,bool)`、`visibleChanged(bool)`。`KeySpec` 含 keysym、keycode、修饰状态。

- [ ] 先写失败测试：鼠标+焦点不显示、触屏+焦点显示、物理键盘收起、手动模式不自动显示。
- [ ] 先写生命周期失败测试：双指 Shift+字母、重复触点编号、取消/焦点变更/后端断开清除按键；
  重复退格在 449 ms 为 0 次，在 450 ms 首次重复；模拟鼠标事件不重复触控输入。
- [ ] 运行 `ctest --test-dir /build -R keyboard-state --output-on-failure`，确认功能缺失导致失败。
- [ ] 实现状态机和虚拟时钟驱动的重复控制；所有停止条件先释放已发送按键，再清理计时器。
- [ ] 运行上述测试通过后提交这组独立代码。

### Task 3: Fcitx5 接入和焦点保护

**Files:** `src/fcitxclient.{h,cpp}`、`src/virtualkeyboardui.{h,cpp}`、`addon/focusbridge.{h,cpp}`、
`addon/agino-keyboard.conf.in`、`tests/test_fcitx.cpp`、`tests/test_bridge.cpp`。

**Interfaces:** Consumes `KeyboardState` 的按键事件；Produces `FcitxClient::sendKey(KeySpec,bool,quint64)`、
`selectCandidate(int,quint64 contextEpoch,quint64 candidateEpoch)`、`requestPage(bool)`、`setInputMethod(QString)`；
properties `connected`、`contextEpoch`、`candidateEpoch`、`editable`、`sensitive`、`preedit`、`candidates`、`inputMethods`。
分页候选按上下文/列表代次与实际页索引保存映射，滚动触发按需载入，拒绝旧页映射提交。

- [ ] 先写失败测试：真实私有 D-Bus 回调解析预编辑和候选；后端消失时清空；争用 UI 服务名时拒绝替换所有者。
- [ ] 先写失败测试：没有焦点拒绝发键，旧 contextEpoch/候选代次拒绝提交，空候选/越界索引不调用后端，
  后端重启不重放旧按键；密码用途清空候选并使用安全模式。
- [ ] 编译并运行 `ctest --test-dir /build -R 'fcitx|bridge' --output-on-failure`，确认失败。
- [ ] 实现 UI 端协议回调与异步连接恢复；会话 addon 同步检查当前真实 InputContext、能力和候选代次，
  仅在校验通过时分派发键/选词。用途来自目标上下文，无法获得时暴露 unknown 并保留手动安全输入。
- [ ] 私有会话使用真实 Fcitx5+拼音验证候选与提交，Qt/GTK 目标应用读回最终字符串；测试全绿后提交。

### Task 4: DTK 风格和触控界面

**Files:** `src/main.cpp`、`qml/KeyboardPanel.qml`、`KeyTile.qml`、`CandidateBar.qml`、
`SettingsWindow.qml`、`tests/qml/tst_keyboard.qml`、`tests/test_qml.cpp`。

**Interfaces:** Consumes Task 2/3 控制器属性；Produces 可绑定非聚焦窗口的 `KeyboardPanel`；
`SettingsWindow` 可编辑模式、键盘高度、目标屏幕与安全输入开关。

- [ ] 先写失败检查：所有可触控按键尺寸 `>= 48`，系统主题切换后文本/背景更新，面板控件不申请焦点，
  设置窗口打开时键盘暂停发键；候选展开采用虚拟化列表。
- [ ] 先写布局失败检查：默认五行、功能行开关默认关闭；开启后六行、F1–F12 各出现一次；
  Esc/Del 随开关分别出现在数字行首/反斜杠之后或功能行且不重复；1–0、Tab/Caps/Ctrl/Super/Alt/标点完整，
  顶部只有窄标题行，设置左/标题中/关闭右；底行无设置/关闭键，设置窗口内包含自动/手动/关闭，预编辑为空时隐藏候选条。
- [ ] 先写候选失败检查：横向拖动后不误选候选，只有一个向下箭头，展开遮挡并禁用全部字母键；
  选择有效候选/再次点箭头还原，焦点或代次改变清空；候选分页加载与索引映射正确；↑/↓ 的中心坐标相等。
- [ ] 先写变宽候选失败检查：单字、2字词、3字词、6字短语的按钮宽度随内容增大、触控高度不小于 48px；
  默认候选行和展开面板均按内容加统一内边距计宽，不均分、不拉伸；默认行连续左对齐且剩余区域留白；
  展开词条自动换行，超过可用宽度的单个候选在按钮内换行；中/英只在 ↑ 右侧出现，右 Shift 保留。
- [ ] 先写关闭按钮失败检查：释放按键、收起候选覆盖层、隐藏面板，不改变模式或结束输入法，同一焦点周期不立刻重弹。
- [ ] 执行 Qt Quick Test，确认相关行为尚不存在导致失败。
- [ ] 使用 `org.deepin.dtk` 的 ControlBackground、Label、ToolButton、Menu、ComboBox、Switch、Slider；
  设置页采用 DTK ApplicationWindow/TitleBar。配色和字体绑定 DTK 系统接口。
- [ ] 多点触控统一归入状态机，字母/符号长按使用面板内备选层，取消不得提交。
  为按键、输入法切换和收起按钮提供可访问名称；普通按键用面板内反馈替代独立弹窗抢焦点。
- [ ] 默认高度目标 40%，根据当前可见键行、候选条和最小 48px 触控尺寸计算实际最小高度；
  低分辨率/大缩放时显示实际生效高度，不静默省略数字行或已开启的功能行。
- [ ] 面板比三稿收窄一列，固定普通字母键首选尺寸，回收右端退格/Del/Enter的部分加宽；
  中/英移到 ↑ 右侧、箭头组左移，底行移除中/英、设置和关闭键；底行腾出的空间用于空格和修饰键；
  候选行和展开面板为内容计宽的词条流，覆盖层展开不改变窗口大小。
- [ ] 在 100/150/200% 缩放、深浅主题与两种强调色下渲染并检查截图；验证旋转后的尺寸和指针映射，测试通过后提交。

### Task 5: X11 与 Wayland 桌面适配

**Files:** `src/desktopadapter.{h,cpp}`、`src/x11adapter.{h,cpp}`、`src/waylandadapter.{h,cpp}`、
`tests/test_desktop.cpp`、`data/agino-keyboard-wayland.desktop`。

**Interfaces:** Produces `DesktopAdapter::attach(QWindow*)`、`setPanelVisible(bool)`、
`setOutput(QScreen*)`、`capabilities()`；signals `inputSourceChanged(InputSource)`、`lockedChanged(bool)`。
X11 fallback `sendCompatibilityKey(KeySpec,bool)` 必须与 Fcitx5 分派互斥。

- [ ] 先写失败测试：隐藏清除保留区域、输出拔插取消触点、缺少窗口角色禁用停靠能力、未知设备来源禁用自动模式。
- [ ] 运行桌面适配测试确认失败，再实现 X11 非聚焦 dock/strut、XI2 设备来源和必要的 XTest 兼容路径。
- [ ] 实现 LayerShellQt bottom anchors、KeyboardInteractivityNone、合理 exclusive zone；
  KWin 启动唯一 Fcitx5，保留 input-method 角色，不与 GUI 共用同一 socket 流。
- [ ] 接入合成器/输入法的实际自动呼出信号，消除鼠标误弹出和虚拟键自触发收起；能力未知时明确降级手动模式。
- [ ] 隔离会话验证 Ctrl/Shift、大写、Tab/Enter/方向键、窗口不抢焦点和最大化可用区域；
  Wayland 原生 Qt/GTK 与 XWayland 应用分别读回输入，验证锁定/解锁后重新取得上下文；通过后提交。

### Task 6: Debian 包和原型集成

**Files:** `scripts/build-screen-keyboard.sh`、`scripts/guest/build-screen-keyboard.sh`、
`data/agino-keyboard.desktop`、`data/copyright`、`scripts/build-prototype.sh`、
`scripts/guest/configure.sh`、`config/packages/desktop.list`、`tests/test_screen_keyboard.py`、`README.md`。

**Interfaces:** Produces `out/components/agino-keyboard/agino-keyboard_0.1.0_amd64.deb`、
源码归档、构建包版本清单和 SHA256SUMS；桌面构建显式安装应用与会话 addon。

- [ ] 先写集成失败测试：缺失产物明确拒绝构建，包包含应用/QML/addon/入口/许可，
  无 root helper/uinput 服务，不覆盖既有输出；运行测试确认失败。
- [ ] 构建脚本沿用隔离容器与失败日志保留方式，源码只读挂载，依赖和许可证按实际动态链接/QML导入填写。
- [ ] 修改桌面原型接入点，默认 X11 自启动只注册隐藏界面；Wayland 入口单独配置，避免重复启动输入法。
- [ ] 执行包构建和全部 CTest，检查 dpkg-deb 内容；安装到一次性 VM 副本验证入口与输入，测试通过后提交。

### Task 7: 完整验收与交付报告

**Files:** `scripts/verify-screen-keyboard.py`、`docs/screen-keyboard-validation.md`、`apps/agino-keyboard/README.md`。

**Interfaces:** Produces 真实输入结果、日志、截图、版本和资源样本；区分通过、失败、未实测。

- [ ] 验证脚本失败时非零退出，报告不得把缺少测试硬件标成通过；先写并运行失败测试。
- [ ] 完成设计验收矩阵，包括 100 次呼出/收起、后端恢复、快速焦点切换、敏感输入和多显示器。
- [ ] 完整 VM Wayland 会话与嵌套会话分别记录；DDE会话阻碍实际排查处理，不能用嵌套结果冒充完整桌面。
- [ ] 在真实触屏二合一设备验证双指、长按、鼠标/实体键盘混用、旋转和休眠；缺少设备时保留明确未验证项。
- [ ] 运行仓库全量 unittest、应用全量 CTest、干净打包及 diff 检查；按原方法测量系统空闲内存，
  另测键盘隐藏/显示/输入状态的资源和唤起延迟，保存新增软件包和体积。
- [ ] 更新使用说明和限制，提交本任务文件；交付可运行包及报告，声明结论严格限于已通过的环境。

## 审阅与执行

架构与 DTK 风格已由用户确认，用户要求的窄标题行和默认候选行内容计宽已写入外观五稿。
用户已确认方案五，主代理在当前会话实施；旧稿底行设置/关闭键及候选均分方式不再属于实施目标。
推荐在当前会话由主代理直接实施：输入法、界面和窗口行为依赖共同接口，连续处理便于定位集成问题。
只有用户选择分工时才转入并行子代理实施。

## 本次执行结果

Tasks 1–6 已实现并通过自动验证；Task 7 完成预览范围的干净构建、完整 DDE X11、嵌套 Wayland/XWayland、三次冷启动资源记录和交付报告。
自动化 13/13 CTest 与 16/16 仓库 unittest 通过；完整 DDE X11 11 项适用测试通过。
完整 DDE Wayland 和实体设备验收仍未实测；三次整机内存均超过 1,000,000,000 字节，发布门槛未通过。
精确版本、样本、范围与限制见 `docs/screen-keyboard-validation.md`。
