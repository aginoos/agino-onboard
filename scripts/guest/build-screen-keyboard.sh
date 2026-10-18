#!/usr/bin/env bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8
# This script runs only inside the disposable build container.
keyboard_version="$(sed -n 's/^project(agino-keyboard VERSION \([^ ]*\) LANGUAGES.*/\1/p' /agino-src/CMakeLists.txt)"
[[ "$keyboard_version" =~ ^[0-9]+[.][0-9]+[.][0-9]+$ ]]
for helper in /usr/bin/ldd /usr/sbin/ldconfig;do if [[ -f "$helper.REAL" ]];then mv "$helper.REAL" "$helper";fi;done
printf '#!/bin/sh\nexit 101\n' >/usr/sbin/policy-rc.d
chmod +x /usr/sbin/policy-rc.d
trap 'rm -f /usr/sbin/policy-rc.d' EXIT
mkdir -p /lastore/override;touch /lastore/override/desktop2uaid
apt-get update
apt-get install -y --no-install-recommends cmake g++ dpkg-dev pkg-config python3 \
 qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-wayland-dev qt6-wayland-private-dev wayland-protocols libdtk6declarative-dev \
 libfcitx5core-dev kglobalacceld libcanberra-dev libcanberra-pulse \
 libxi-dev libxtst-dev libwayland-dev libgtk-3-dev fcitx5-chinese-addons \
 fcitx5-frontend-qt6 fcitx5-frontend-gtk3 qml6-module-qtquick-window qml6-module-qttest dbus-x11 xvfb xauth xcompmgr pulseaudio pulseaudio-utils \
 kwin-wayland kwin-dev libkf6config-dev xwayland fonts-noto-cjk \
 extra-cmake-modules gettext libfmt-dev libjson-c-dev libxkbcommon-dev libevent-dev \
 libsystemd-dev libuv1-dev libdbus-1-dev libuuid1 uuid-dev \
 libxcb-util-dev libxcb-xkb-dev libxcb-ewmh-dev libxcb-keysyms1-dev libxcb-xfixes0-dev \
 libxcb-randr0-dev libxcb-xinerama0-dev libxcb-icccm4-dev libxcb-imdkit-dev libxkbfile-dev libxkbcommon-x11-dev
# Ensure the integration tests cannot accidentally load an old external shell.
# This affects only this disposable build container, never the host desktop.
layer_packages=()
for package in layer-shell-qt liblayershellqtinterface-dev liblayershellqtinterface6 qml6-module-org-kde-layershell;do
 if [[ "$(dpkg-query -W -f='${db:Status-Status}' "$package" 2>/dev/null || true)" == installed ]];then layer_packages+=("$package");fi
done
if (( ${#layer_packages[@]} ));then apt-get remove -y "${layer_packages[@]}";fi
if [[ ! -s /etc/machine-id ]];then dbus-uuidgen >/etc/machine-id;fi
# The private compositor SDK and patched frontend have release-specific ABIs.
[[ "$(dpkg-query -W -f='${Version}' kwin-wayland)" == 4:6.1.38 ]]
[[ "$(dpkg-query -W -f='${Version}' fcitx5)" == 5.1.12-2deepin12 ]]
sed -n 's/^deb /deb-src /p' /etc/apt/sources.list >/etc/apt/sources.list.d/agino-build-source.list
apt-get update
mkdir -p /upstream;cd /upstream
apt-get source fcitx5=5.1.12-2deepin12 kwin=4:6.1.38
patch -d /upstream/fcitx5-5.1.12 -p1 </agino-src/patches/fcitx5-5.1.12-paired-virtual-keys.patch
patch -d /upstream/fcitx5-5.1.12 -p1 </agino-src/patches/fcitx5-5.1.12-x11-focus-group.patch
cmake -S /upstream/fcitx5-5.1.12 -B /frontend-build -DCMAKE_BUILD_TYPE=Release -DENABLE_TEST=OFF -DENABLE_TESTING_ADDONS=OFF -DENABLE_DOC=OFF -DENABLE_LIBUUID=ON -DENABLE_ENCHANT=OFF -DENABLE_X11=ON -DENABLE_XIM=OFF -DCMAKE_SKIP_RPATH=ON
cmake --build /frontend-build --target waylandim xcb -j2
cmake -S /agino-src -B /build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DAGINO_WAYLAND_MODULE=/frontend-build/src/frontend/waylandim/libwaylandim.so -DAGINO_KWIN_SOURCE=/upstream/kwin-6.1.38 -DAGINO_XCB_MODULE=/frontend-build/src/modules/xcb/libxcb.so
cmake --build /build -j2
# Test Fcitx loads the actual new addon in this container's private sessions.
cmake --install /build
ctest --test-dir /build --output-on-failure
DESTDIR=/stage cmake --install /build --strip
mkdir -p /stage/DEBIAN /stage/usr/share/doc/agino-keyboard /result /build/debian
install -m644 /agino-src/README.md /agino-src/COPYING /stage/usr/share/doc/agino-keyboard/
install -m644 /upstream/fcitx5-5.1.12/debian/copyright /stage/usr/share/doc/agino-keyboard/fcitx5-copyright
install -m644 /upstream/kwin-6.1.38/debian/copyright /stage/usr/share/doc/agino-keyboard/kwin-copyright
mkdir -p /result/upstream
cp /upstream/*.dsc /upstream/*.tar.* /result/upstream/
(cd /result/upstream && sha256sum * > SHA256SUMS)
cat >/build/debian/control <<'CONTROL'
Source: agino-keyboard
Section: utils
Priority: optional
Maintainer: Agino Project <agino@localhost>
Package: agino-keyboard
Architecture: any
Description: Input method integrated touchscreen keyboard
CONTROL
(cd /build && dpkg-shlibdeps -O -e/stage/usr/bin/agino-keyboard -e/stage/usr/libexec/agino-keyboard-ui -e/stage/usr/libexec/agino-keyboard-dialog -e/stage/usr/lib/agino-keyboard/fcitx5/libxcb.so -e/stage/usr/lib/x86_64-linux-gnu/fcitx5/libagino-keyboard-bridge.so -e/stage/usr/lib/x86_64-linux-gnu/fcitx5/libagino-waylandim.so -e/stage/usr/lib/x86_64-linux-gnu/qt6/plugins/kwin/plugins/libagino-keyboard-device.so -e/stage/usr/lib/x86_64-linux-gnu/qt6/plugins/wayland-shell-integration/libagino-keyboard-shell.so > /tmp/keyboard-shlibs)
dependencies="$(sed -n 's/^shlibs:Depends=//p' /tmp/keyboard-shlibs)"
cat >/stage/DEBIAN/control <<CONTROL
Package: agino-keyboard
Version: $keyboard_version
Architecture: amd64
Maintainer: Agino Project <agino@localhost>
Section: utils
Priority: optional
Depends: $dependencies, kglobalacceld, libcanberra0, libcanberra-pulse, fcitx5 (= 5.1.12-2deepin12), fcitx5-modules (= 5.1.12-2deepin12), kwin-common (= 4:6.1.38), qt6-base-private-abi (= 6.8.0), qt6-waylandclient-private-abi (= 6.8.0), qt6-wayland
Description: Agino input method integrated touchscreen keyboard
 Native DTK6 keyboard with guarded Fcitx5 input, touch and KWin Wayland support.
CONTROL
(cd /stage && find usr etc -type f -print0 2>/dev/null | sort -z | xargs -0 md5sum) >/stage/DEBIAN/md5sums || [[ ! -d /stage/etc ]]
printf 'Installed-Size: %s\n' "$(du -sk /stage | cut -f1)" >>/stage/DEBIAN/control
dpkg-deb --root-owner-group -b /stage /result/agino-keyboard_${keyboard_version}_amd64.deb
dpkg-query -W -f='${Package}\t${Version}\n' | sort >/result/build-packages.tsv
cp /build/Testing/Temporary/LastTest.log /result/test.log

python3 /agino-src/tools/verify-package.py /result/agino-keyboard_${keyboard_version}_amd64.deb | tee /result/dependency-check.log
