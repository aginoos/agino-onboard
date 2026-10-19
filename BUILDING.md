# 构建与测试

当前版本 0.1.24，正式前端为 QtGui/QPainter。代码为匹配的 Deepin Qt6/DTK6/Fcitx5/KWin 环境开发，不是任意发行版上的通用二进制包。

## 环境

- CMake ≥3.21、C++17 编译器、pkg-config、Python 3。
- Qt 6.8.0（当前 Wayland private API 精确约束）、Qt Base/Wayland 私有开发头文件；DTK6 Gui/Declarative。
- Qt QML/Quick/QuickControls2/QuickTest：用于保留的回归测试，正式界面不加载 QML。
- Fcitx5 Core/Utils、libcanberra、Wayland client/server、Wayland protocols、X11/Xi/Xtst、xcb/xcb-xfixes、GTK3 开发包。
- 完整会话测试需要 Xvfb、xauth、DBus、KWin、XWayland、Fcitx5、拼音、GTK/Qt 输入法前端、字体和音频测试环境。
- 真实 AT-SPI 测试需要 at-spi2-registryd、Python pyatspi/GI；搜狗测试仅在自行安装匹配搜狗的隔离环境启用，不附带搜狗二进制。

依赖安装列表见 `scripts/guest/build-screen-keyboard.sh`。该脚本只供一次性构建容器运行，会修改容器包和输入法模块；不要直接在日常主机运行。

## 编译

```sh
cmake -S apps/agino-keyboard -B build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

源码默认同时构建测试。完整包还需要匹配 Deepin Fcitx5 5.1.12-2deepin12 / KWin 6.1.38 的插件：

- `AGINO_KWIN_SOURCE`：准确匹配的 KWin 源码目录。
- `AGINO_WAYLAND_MODULE`：通过仓库补丁构建的匹配 Fcitx Wayland 模块。
- `AGINO_XCB_MODULE`：通过仓库补丁构建的匹配 Fcitx XCB 模块。

完整 Debian 包的下载、补丁、构建、许可复制和打包步骤位于 `scripts/guest/build-screen-keyboard.sh`，宿主入口为 `scripts/build-screen-keyboard.sh`。宿主入口需要 `AGINO_KEYBOARD_BUILD_IMAGE` 指定匹配构建镜像，或已有 `out/agino-rootfs-amd64.tar.gz`；本仓库不分发该镜像或原发行版生成工具。

测试中无硬件、真实 DDE 会话等条件的项目不能据此宣称已通过。当前发布验收及内存数据见 `docs/`，它们明确区分构建测试与真实硬件验收。
