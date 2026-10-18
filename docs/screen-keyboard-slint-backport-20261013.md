# Slint 0.2.3 向 Qt/DTK 回移

用户目标：读取“用 Slint 实现屏幕键盘应用”会话和实现，将全部新增功能/修复移植到 Qt/DTK，功能、界面以最新 Slint 为准。Qt/DTK 技术栈不变。源基线 a16ee89d（Slint 0.2.3），目标 e9cb20c2（Qt 0.1.15）。本轮独立分支 codex/slint-parity，原 master 保持。服务器/发行版仍暂停，未恢复部署。

## 已核对范围

已读取指定会话全部可分页内容、Slint 当前源码、0.2.1/2/3修复报告和原 Qt 对齐审计；三个只读子审计覆盖 UI、输入后端及平台。Fcitx/Sogou 两个桥接源文件逐字相同。Rust 独有的 Xsettings 色值解析、AccessKit补丁、Slint渲染缓冲和字体拟合实现不移植；使用 Qt/DTK 原生机制达成可见/功能行为。

## 实施与验收顺序

1. Shift 单击一次性状态/双击符号锁定/↑标记，修饰键同符号触点去重；候选状态隐藏并释放宽度；不透明度50–100及旧配置归一；副符号12/10px及颜色、失效文字变暗；mode/theme读取归一。
2. 元数据独立owner代次、有界手工刷新、输入法列表通知、断连周期恢复、select/page错误恢复与过期完成；可信Hide刷新权威状态。
3. Qt原生辅助功能动作与身份/策略门禁：键、候选、变音、分段、选择器/快捷键/透明度，覆盖区域不暴露下面的键；按键声音仅接受的输入触发。
4. 打开的设置选择器实时刷新且稳定身份；敏感/不可输入状态不切换输入法；快捷键注册失败可观察；拒绝布局先释放面板。
5. 浮动窗口在compositor协商尺寸后保留下边；排队原生拖动回归（仅若真实复现才修Qt坐标来源）；隔离激活环境。
6. 同一构建验证原全套相关测试、与Slint参照的实际UI/输入截图（标准/紧凑、Fn、明暗、100/150/200、候选/展开/设置/选择器/Shift、50/100透明度）；按实际证据说明边界，不能将源码同构当实物触屏/热插拔或逐像素证明。
7. 0.1.16包、源码与本地Git；不自动安装、不上传，不纳入共享主工作区其他WIP。

## 当前已执行

- 新状态用例修复前3项失败：单击Shift不保留、锁定数字未移位、同符号modifier重复发送。修复后状态22项通过。
- 新界面用例修复前2项失败：候选状态仍显示、透明度最小30；修复后QML39项通过。
- 上述为首次阶段记录，后续实现与最终验收见下文。证据 out/components/agino-keyboard-validation/slint-backport-016；原失败保留。

## 后续实现事实（验收进行中）

- 元数据控制器单独owner丢失/重新获得、延迟旧回复拒绝、同一owner临时snapshot错误恢复及选择/分页传输错误断连的新用例已经通过；元数据当前10项通过。手工刷新重新取IDs并重置有界重试，可信IM列表事件接通；可信Hide请求权威snapshot并暂停输入。
- 新Qt原生读屏动作已接键/分段/选择器/候选/变音/分页/透明度；同内容新候选代次需要更新委托身份的回归先失败，刷新模型并保留滚动后QML43项通过。额外透明度读屏写入回归先失败，已补动作与持久设置桥，仍待后续完整运行。
- 声音只在SendKey被接受进入发送路径时触发；已补默认关闭不映射libcanberra的失败用例，改为工作线程首次播放时QLibrary加载，尚待完整音频复验。
- Wayland控制peer实际复现下边700→756（56px）；增加原生协商尺寸回传、保留下边及重复尺寸请求抑制。第一次验证误加载SDK中旧插件，保留失败；准确/build插件的专项测试通过。尚待完整/缩放复验。
- 原生X11排队鼠标测试（私有窗口SIGSTOP入队六次motion再恢复）100%通过，末端坐标及静止指针一致；没有为Slint独有坐标问题改Qt的mapToGlobal。
- Fcitx设置写入新增保留原文本及原子提交；原QSettings用#注释文件的失败已记录，旧实现会报告格式错误，本轮改为QSaveFile，尚待复验。
- 所有新私有D-Bus测试在启动bus前先隔离runtime/config/data/cache，未读用户输入或改变宿主配置。编译只在现有owned SDK容器，本机源码/文档维护；无服务器操作。

## 修复对应表

| Slint 差异/修复 | Qt/DTK 对应实现 |
|---|---|
| 单击 Shift 一次性修饰、双击锁定、↑ Shift、锁定数字标点 | KeyboardState 锁存/解析及 KeyTile 显示；同符号修饰触点只发送首按/末放 |
| 候选占满状态文字腾出的宽度、16/13px候选及12/10px副符号 | CandidateBar 动态内容宽度；KeyTile 文字/高亮颜色 |
| 不透明度50–100、旧值及mode/theme归一 | Preferences 读写与持久化归一，设置slider/读屏动作 |
| 选中设置文字不可见 | Qt原生DTK调色板不存在Rust CARD16解析缺陷；明暗/三缩放实际截图可见 |
| 排队拖动过冲 | Qt基线真实排队鼠标测试无该缺陷；保留mapToGlobal，100/150测试固定最终位置 |
| 原生读屏按键及覆盖层/旧候选门禁 | Qt Accessible 动作；候选委托绑定不可变身份、可见/编辑/敏感/拖动校验 |
| 设置选择器动态ID/屏幕/名称、选中态同步 | 打开时及相关notify实时重建，保存滚动与按下身份 |
| 敏感目标或无编辑目标不切换输入法 | 地球键/读屏路径统一 inputAllowed/editable/sensitive 校验 |
| 快捷键校验/注册失败可观察 | KGlobalAccel返回值与Status.shortcutError，非法序列注销 |
| 默认关闭声音不映射音频库，仅接受输入发声 | libcanberra工作线程首次有效播放再加载，显式deb运行依赖 |
| 独立控制器owner代次、有界重试与手工刷新 | FcitxClient namesRevision/owner；新IDs取消旧回复并重新查询 |
| 同owner断连恢复、可信Hide刷新目标、IM列表通知 | 2s恢复计时、权威snapshot门禁与可信NotifyIMListChanged |
| 选择/翻页错误与过期完成 | 错误使后端断连；独立翻页序号，换焦点释放并拒绝旧完成 |
| 原子更新Fcitx开关，写失败显示原因 | QSaveFile保留注释/其他选项，不替换用户profile |
| 尺寸拒绝先释放资源、Off不分配界面 | 先验证目标尺寸、防重入，下一轮事件中卸载再显示警告；Show早拒绝Off |
| 浮动底边跟随compositor确认尺寸 | 自有shell回传NegotiatedSize，按实际高度保底边；停靠仍及时更新预留 |
| Rust渲染/AccessKit/Xsettings实现 | 使用Qt/DTK原生功能；不搬入Rust运行时或Slint绘制缓冲 |

## 实际验证及保留的原失败

- 新同context翻页及新IM列表用例：正确独立fixture时修复前2失败，修复后元数据12项通过。早期fixture误用本地延迟DBus及错误sender的问题仅为测试错误，原记录完整保留。
- 可见紧凑窗口改为标准布局（800×600私有显示）：原实现崩溃；首次只加return还不足以处理resize信号重入。最终先判断目标尺寸、防重入，卸载延后到安全事件轮并发生在警告显示之前，专项通过释放/存活/关闭警告后重新呼出。Off模式原先Show分配面板，补早拒绝后通过。
- 首轮39项完整检查有停靠增高、私有目录清理竞争及Sogou GTK偶发过期选择；停靠改为立即更新exclusive zone，目录清理增加有界重试；后续40项中的原有项目均已通过，Sogou未改桥接代码。该轮仅新尺寸警告失败，再按上述真实复现修复；日志没有覆盖或隐去。
- 关闭声音不加载libcanberra；开启声音使用私有Pulse null sink实际录音并检查非零样本；未连接宿主音频。支持挂起音频端点的关闭动作不阻塞GUI。
- 两版使用同一已安装匹配Fcitx、真实GTK光标目标及私有合成器，均实际鼠标点击得到 `AbA!@#$%!1`。每版取得31张实际窗口像素图：标准/紧凑、100/150/200缩放、Fn、明暗、空候选/候选/展开/设置/输入法选择器及锁定Shift；所有对应逻辑/物理尺寸相同，视觉检查布局、状态和选中文字正常。另做50%不透明度对应截图，平坦背景像素Qt124/Slint123，符合两渲染器整数混合舍入差异。
- Qt透明清屏时最外描边留下黑线，改为先将描边色合成到面板底色，真实原生边缘由0变为218；Slint对应217。保留图像和QML明暗复验。
- 本轮保持原生DTK风格和功能/布局契约；不声称两个绘制器文字抗锯齿及边缘舍入逐像素一致。真实触屏/笔、DDE整会话、旋转/热插拔/休眠仍需设备验收；嵌套KWin和QML多触点测试不替代硬件。
- 所有运行仅在现有匹配SDK容器及私有Xvfb/DBus/XDG环境；未安装本机、没有变更用户输入法或恢复服务器。

## 最终交付验收

最终二进制完成重新编译，描边/尺寸生命周期变更后的QML明暗、尺寸/Off/警告恢复、启动和快捷键5项专项通过；其后最终全40项再次运行全部通过，338.30秒，原失败日志保留（ctest-release.log）。包六个ELF检查通过，无LayerShellQt依赖；libcanberra0为延迟加载所需的显式依赖。APT模拟仅替换本机agino-keyboard-slint 0.2.3为Qt版，不新增其他依赖。最终候选安装包463128字节，Installed-Size2228KiB；不含已安装共享依赖占用。

实际执行命令（工作树 /home/deepin/.codex/worktrees/screen-keyboard/agino-os）：

```sh
docker exec agino-keyboard-build cmake --build /build -j1
docker exec agino-keyboard-build cmake --install /build
docker exec agino-keyboard-build ctest --test-dir /build --output-on-failure
docker exec agino-keyboard-build bash /tmp/package-016-release.sh
docker exec agino-keyboard-build xvfb-run -a -s '-screen 0 2560x1600x24' /source/tests/private-bus-session.sh /source/tests/private-session.sh python3 /tmp/capture-parity-016.py
apt-get -s install ./out/components/agino-keyboard-validation/slint-backport-016/agino-keyboard_0.1.16_release_amd64.deb
```

完整日志/截图、最终截图脚本与实际打包脚本位于 out/components/agino-keyboard-validation/slint-backport-016；该目录是忽略的构建证据，不纳入产品源码Git。源码归档包含应用、补丁和构建脚本；complete-sources另含已核验匹配的Fcitx/KWin原始档案。包内strip后的主程序与当前/build二进制做相同strip后的内容逐字相同；包内主程序实际点击/候选/设置捕获也通过。
