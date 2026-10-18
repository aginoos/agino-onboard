#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
keyboard_version="$(sed -n 's/^project(agino-keyboard VERSION \([^ ]*\) LANGUAGES.*/\1/p' "$project_dir/apps/agino-keyboard/CMakeLists.txt")"
[[ "$keyboard_version" =~ ^[0-9]+[.][0-9]+[.][0-9]+$ ]]
output_dir="${AGINO_OUTPUT_DIR:-$project_dir/out}"
build_dir="$output_dir/components/agino-keyboard"
archive="$output_dir/agino-rootfs-amd64.tar.gz"
[[ ! -e "$build_dir" ]] || { echo "Build output exists: $build_dir" >&2;exit 1; }
if [[ -z "${AGINO_KEYBOARD_BUILD_IMAGE:-}" ]];then
 [[ -r "$archive" ]] || { echo "Missing rootfs archive: $archive" >&2;exit 1; }
 image="agino-os/bootstrap:$(sha256sum "$archive" | cut -c1-12)"
else image="$AGINO_KEYBOARD_BUILD_IMAGE";fi
for tool in docker tar sha256sum;do command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2;exit 1; };done
docker info >/dev/null
mkdir -p "$output_dir/components"
mkdir "$build_dir"
build_dir="$(cd "$build_dir" && pwd)"
if ! docker image inspect "$image" >/dev/null 2>&1;then
 [[ -z "${AGINO_KEYBOARD_BUILD_IMAGE:-}" ]] || { echo "Missing build image: $image" >&2;exit 1; }
 docker import "$archive" "$image" > "$build_dir/import.log"
fi
docker image inspect "$image" --format '{{.Id}}' > "$build_dir/build-image.txt"
tar --exclude=__pycache__ --exclude='*.pyc' -czf "$build_dir/agino-keyboard-${keyboard_version}-source.tar.gz" -C "$project_dir/apps" agino-keyboard
container=""
cleanup(){ local status=$?;if [[ -n "$container" ]];then if [[ "$status" == 0 ]];then docker rm -f "$container" >/dev/null;else docker stop -t 3 "$container" >/dev/null 2>&1 || true;printf '%s\n' "$container" > "$build_dir/failed-container.txt";echo "Stopped build container retained: $container" >&2;fi;fi;}
trap cleanup EXIT
container="$(docker create --network host --mount "type=bind,src=$project_dir/apps/agino-keyboard,dst=/agino-src,readonly" --mount "type=bind,src=$project_dir/scripts/guest/build-screen-keyboard.sh,dst=/agino-build.sh,readonly" --mount "type=bind,src=$build_dir,dst=/result" -e http_proxy -e https_proxy -e HTTP_PROXY -e HTTPS_PROXY -e no_proxy -e NO_PROXY "$image" bash /agino-build.sh)"
echo "Building screen keyboard; log: $build_dir/build.log"
if ! docker start -a "$container" > "$build_dir/build.log" 2>&1;then tail -n 45 "$build_dir/build.log" >&2;exit 1;fi
[[ "$(docker inspect -f '{{.State.ExitCode}}' "$container")" == 0 ]] || { tail -n 45 "$build_dir/build.log" >&2;exit 1; }
(cd "$build_dir" && sha256sum agino-keyboard_${keyboard_version}_amd64.deb agino-keyboard-${keyboard_version}-source.tar.gz > SHA256SUMS)
echo "Package: $build_dir/agino-keyboard_${keyboard_version}_amd64.deb"
