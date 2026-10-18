# 屏幕键盘内存测量与优化（2026-10-03）

本页保留第一轮优化的测量与包版本。后续持续收起回收、逐项实验和当前交付见 [收起优化报告](screen-keyboard-idle-optimization.md)。

方案五的布局与 DTK 配色保留。第一轮安装包输入时 RSS 下降约 57%，PSS 下降约 65%，USS 下降约 67%。

## 同环境对照

自有 KVM 虚拟机，Deepin 25 / 完整 DDE X11，4 GiB 内存、2 vCPU、1920×1080。优化前后各启动 3 个全新 GUI 进程，取各状态中位数；下表单位为 MiB（1,048,576 字节）。已有 Fcitx 与桌面进程不计入 GUI 进程数值。

| 状态 | 原 RSS | 新 RSS | 原 PSS | 新 PSS |
| --- | ---: | ---: | ---: | ---: |
| 隐藏启动 | 55.4 | 55.4 | 12.6 | 12.6 |
| 显示键盘 | 193.4 | 91.3 | 92.6 | 37.1 |
| 拼音输入，128 个候选词 | 224.7 | 96.2 | 115.7 | 40.4 |
| 输入后收起 5 秒 | 224.7 | 96.2 | 115.7 | 40.4 |

| 输入时指标 | 原值 | 新值 | 降幅 |
| --- | ---: | ---: | ---: |
| RSS | 224.7 MiB | 96.2 MiB | 57.2% |
| PSS | 115.7 MiB | 40.4 MiB | 65.1% |
| USS | 84.0 MiB | 27.7 MiB | 67.0% |

RSS 包括驻留的共享页；PSS 按共享进程数分摊；USS 为 Private_Clean + Private_Dirty。使用 /proc/PID/smaps_rollup 统计总量，以 smaps 按映射归组，只读取内存元数据，不读取用户输入或进程内存内容。

首次本地 DBus Show 调用的中位数由 109.6 ms 降至 70.4 ms。这是调用返回耗时，包含窗口/QML 创建，并非完整首帧时间。

展开更多候选词时，新包 RSS 105.6 MiB、PSS 49.2 MiB；展开后再收起，分别为 105.6、49.3 MiB。在候选条状态下，每个进程做 10 次快速呼出/收起，第 1、5、10 次 PSS 都约 40 MiB，没有观察到持续增长。

## 原因与修复

1. 默认图形路径在该虚拟机加载 OpenGL/LLVM。显示状态的图形库 PSS 约 17.8 MiB，同时堆和匿名映射显著增加。只改软件渲染的独立对照，把输入时 PSS 从 115.7 降至 56.3 MiB；这证明主要差异来自渲染路径，不应将全部 RSS 归因于控件或输入法词库。
2. 折叠的更多候选词面板原来仍实例化并测量全部候选词。改为展开时用 Loader 创建，选择或收起后销毁。这进一步降低常用候选条状态的字体访问和对象分配。候选条计宽、横向滑动、分页、选择代次检查保持不变。
3. 隐藏满 1 秒后请求释放可重建的渲染资源；快速重新呼出取消回收，QML 对象与输入状态保留。立即回收的实验在 OpenGL 短循环中出现更高占用，已用延迟回收避免反复重建。修复并验证了窗口析构期间的计时器回调生命周期。

软件渲染适用于本应用的二维按键与文字，最终浅/深色、设置、Fn 行、候选展开、100/150/200% 截图已检查。显式后端变量保持有效，例如 `QSG_RHI_BACKEND=opengl agino-keyboard`。软件后端不能渲染 ShaderEffect，分数缩放会放弃局部重绘；真实设备上的图形性能仍需验收。[Qt 6.8 软件渲染说明](https://doc.qt.io/qt-6.8/qtquick-visualcanvas-adaptations-software.html)

资源释放是 Qt 后端的请求/提示，不等于整个进程复位。[Qt 6.8 QQuickWindow 资源管理](https://doc.qt.io/qt-6.8/qquickwindow.html#releaseResources) 字体、QML 和分配器缓存仍会保留；默认软件路径收起后的 PSS 没有明显下降，因此不声称收起即可返回 12.6 MiB 的启动水平。显式 OpenGL 对照输入后收起的 RSS 从约 202.5 降到 188.9 MiB，但在该虚拟机仍比默认路径高。

## 验证与交付

- 应用源码提交：`5d3b8202400b86fc3d2474201263d95a5dd88acf`。干净构建的源码归档逐文件匹配，47 个文件。
- 干净构建 15/15 CTest 通过，覆盖真实输入、候选、安全输入、触控模拟、嵌套 KWin Wayland v2/v3、XWayland、后端恢复、焦点/停靠与资源释放。
- 新增软件渲染的真实像素测试：隐藏超过回收延迟后，保留的 QML 对象更新颜色，再呼出时正确绘制。
- 仓库 16/16 unittest 通过；新包安装并重启匹配的 Fcitx 后，完整 DDE X11 测试 11 通过、0 失败、7 个私有/其他环境用例按条件跳过。
- 独立代码审阅无 Critical/Important 问题；Show 耗时已明确标为调用返回耗时，软件重绘证据已补充。测试夹具的一次失败来自后台服务清理目录时的并发写入，保留原日志，并通过等待自有进程及有限清理重试修复。

新 Debian 包：383,316 字节（约 374 KiB），Installed-Size 1,928 KiB（约 1.88 MiB）；复用系统 Qt/DTK/Fcitx。SHA256：`fc64584b50e122b8dd37403eb134d1fd45efc9b217adbb6b52c3ab5affcd62e2`。前一包及全部原始数据保留，旧包 SHA256：`b6b53ba179614b6b67c73b9751ce85d771f459ec9ea357443a391ead76e9dde9`。

本轮仅测量和优化应用进程。前一包三次整机冷启动的 1,000,000,000 字节门槛未通过，新包没有重新进行这一整机试验。真实触屏/笔、旋转、热插拔、休眠和完整 DDE Wayland 会话依然未验证；`release_ready=false`。审阅中暂不扩展这些独立验收，也不重写输入协议或更换 DTK/QML。

## 原始证据

- [统计汇总](../out/components/agino-keyboard-validation/memory-optimization/summary.json)
- [优化前测量](../out/components/agino-keyboard-validation/memory-optimization/baseline.json)
- [最终安装包测量](../out/components/agino-keyboard-validation/memory-optimization/optimized-package.json)
- [测量脚本](../out/components/agino-keyboard-validation/memory-optimization/probe-package.py)
- [完整 DDE X11 新包验证](../out/components/agino-keyboard-validation/memory-optimization/dde-x11-package-tests.log)
- [软件绘制恢复测试](../out/components/agino-keyboard-validation/memory-optimization/software-reopen.log)
- [实际候选展开截图](../out/components/agino-keyboard-validation/memory-optimization/captures/expanded.png)

应用源码归档及匹配的签名上游源码、补丁、许可证随完整交付包提供。
