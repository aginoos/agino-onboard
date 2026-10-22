# Agino Onboard

Agino 屏幕键盘，当前 Qt 版本 **0.1.25**。支持 X11、Wayland、触屏、Fcitx5 候选词集成和标准/紧凑布局。界面使用 QtGui/QPainter，遵循 Deepin/DTK 风格。

## 0.1.25

- 默认手动呼出，不随登录启动。用户从启动器运行屏幕键盘；在键盘设置里选择自动模式后，才创建用户自己的登录自启动项，改回手动或关闭模式会移除该托管项。用户自行编写的同名项不覆盖。
- 修复登录/锁屏嵌入键盘在受影响的 X11/virgl 环境里显示后变黑：嵌入窗口使用不透明像素格式，并保留原生圆角裁剪。桌面键盘的透明效果和有界分条绘制不变。
- 新增真实嵌入窗口的回归检查，覆盖缩放 1 和 2、宿主重挂、隐藏/显示、清空后的重绘，以及关闭标准输入后正常退出。构建检查需要 Xvfb 和 xauth。

- [应用功能、依赖与编译说明](apps/agino-keyboard/README.md)
- [原生前端 0.1.21 的历史验证记录](docs/screen-keyboard-standard-cursor-021.md)
- [最近内存复测](docs/screen-keyboard-memory-remeasurement-021.md)

## 编译

```sh
cmake -S apps/agino-keyboard -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

完整依赖和测试环境见 [构建说明](BUILDING.md)。编译依赖及 Fcitx5/KWin 的 ABI 要求见应用说明。容器打包入口为 `scripts/build-screen-keyboard.sh`，需要匹配的预配置构建镜像（`AGINO_KEYBOARD_BUILD_IMAGE`）或原 rootfs 归档。不会将构建镜像、系统源码归档、安装包、凭据或其他发行版组件上传到此仓库。

## 来源与许可

本仓库首次导入为现有项目的屏幕键盘源码快照，来源提交 `2877f770c5cb48fdbf6c216bd726173fe9bf64f5`；应用与构建脚本内容保持一致；根目录测试仅排除了不属于此仓库的发行版镜像构建用例，详见 SOURCE-PROVENANCE.json。历史文档中的旧提交编号指原开发仓库；当前清理范围见 [清理记录](docs/repository-cleanup.md)。

许可见 [COPYING](apps/agino-keyboard/COPYING)，第三方组件保留各自版权及许可。
