#!/bin/bash
# Build a QuickShareLinux AppImage.
#
# Expects Qt 6.10+ on QT_ROOT_DIR (install-qt-action sets this) and a C++20 compiler.
# Abseil, protobuf, nlohmann-json and sdbus-c++ are built into $DEPS from the same
# tarballs as packaging/flatpak/top.suhasdissa.QuickShareLinux.yml. Keep the pins together.
#
# Usage: VERSION=0.1.0 QT_ROOT_DIR=/opt/qt packaging/appimage/build.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${VERSION:?VERSION is required}"
QT_ROOT_DIR="${QT_ROOT_DIR:?QT_ROOT_DIR is required}"
DEPS="${DEPS:-${ROOT}/.deps}"
DIST="${DIST:-${ROOT}/dist}"
JOBS="${JOBS:-$(nproc)}"
STAMP_WANT="abseil-20260817.0 protobuf-36.1 json-3.12.0 sdbus-2.3.1"

fetch() {
  local url="$1" sha="$2" dest="$3"
  mkdir -p "$(dirname "$dest")"
  if [[ ! -f "$dest" ]] || ! echo "${sha}  ${dest}" | sha256sum -c -; then
    curl -fL --retry 3 --retry-delay 2 -o "${dest}.partial" "$url"
    mv "${dest}.partial" "$dest"
    echo "${sha}  ${dest}" | sha256sum -c -
  fi
}

extract() {
  local archive="$1" dest="$2"
  rm -rf "$dest"
  mkdir -p "$dest"
  tar -xf "$archive" -C "$dest" --strip-components=1
}

build_cmake() {
  local src="$1"
  shift
  cmake -S "$src" -B "${src}/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$DEPS" \
    -DCMAKE_PREFIX_PATH="$DEPS" \
    "$@"
  cmake --build "${src}/build" --parallel "$JOBS"
  cmake --install "${src}/build"
}

if [[ ! -x "${QT_ROOT_DIR}/bin/qmake" ]]; then
  echo "qmake not found under QT_ROOT_DIR=${QT_ROOT_DIR}" >&2
  exit 1
fi

mkdir -p "$DEPS/src" "$DIST"

if [[ ! -f "${DEPS}/.stamp" || "$(cat "${DEPS}/.stamp")" != "$STAMP_WANT" ]]; then
  fetch \
    "https://github.com/abseil/abseil-cpp/releases/download/20260817.0/abseil-cpp-20260817.0.tar.gz" \
    "f7e05179df39c45434cad433f5783840bb3788ef322976f9138bc6b72b3a107d" \
    "${DEPS}/src/abseil.tar.gz"
  fetch \
    "https://github.com/protocolbuffers/protobuf/releases/download/v36.1/protobuf-36.1.tar.gz" \
    "dc74fa582f559cbd31614ddfefb4868f43c919d7184bde514bb47f90c6025eb8" \
    "${DEPS}/src/protobuf.tar.gz"
  fetch \
    "https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz" \
    "42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa" \
    "${DEPS}/src/json.tar.xz"
  fetch \
    "https://github.com/Kistler-Group/sdbus-cpp/archive/refs/tags/v2.3.1.tar.gz" \
    "3a289eded586c26d06c1387de72c7bf7c809527a70d51ba6401fe61059b19626" \
    "${DEPS}/src/sdbus.tar.gz"

  extract "${DEPS}/src/abseil.tar.gz" "${DEPS}/src/abseil"
  extract "${DEPS}/src/protobuf.tar.gz" "${DEPS}/src/protobuf"
  extract "${DEPS}/src/json.tar.xz" "${DEPS}/src/json"
  extract "${DEPS}/src/sdbus.tar.gz" "${DEPS}/src/sdbus"

  build_cmake "${DEPS}/src/abseil" \
    -DCMAKE_CXX_STANDARD=20 \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DABSL_PROPAGATE_CXX_STD=ON \
    -DABSL_BUILD_TESTING=OFF \
    -DABSL_ENABLE_INSTALL=ON \
    -DBUILD_SHARED_LIBS=ON
  build_cmake "${DEPS}/src/protobuf" \
    -DCMAKE_CXX_STANDARD=20 \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_ABSL_PROVIDER=package \
    -DBUILD_SHARED_LIBS=ON
  build_cmake "${DEPS}/src/json" \
    -DJSON_BuildTests=OFF
  build_cmake "${DEPS}/src/sdbus" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=ON \
    -DSDBUSCPP_BUILD_CODEGEN=ON \
    -DSDBUSCPP_BUILD_DOCS=OFF \
    -DSDBUSCPP_BUILD_TESTS=OFF \
    -DSDBUSCPP_BUILD_EXAMPLES=OFF

  echo "$STAMP_WANT" > "${DEPS}/.stamp"
fi

export PATH="${DEPS}/bin:${PATH}"
export PKG_CONFIG_PATH="${DEPS}/lib/pkgconfig:${DEPS}/lib64/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="${DEPS}/lib:${DEPS}/lib64:${QT_ROOT_DIR}/lib:${LD_LIBRARY_PATH:-}"
export CMAKE_PREFIX_PATH="${DEPS};${QT_ROOT_DIR}"

cmake -S "$ROOT" -B "${ROOT}/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}" \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DQUICKSHARE_BUILD_TESTS=OFF \
  -DQUICKSHARE_BUILD_CLI=OFF \
  -DQUICKSHARE_QML_SOURCE_DIR=
cmake --build "${ROOT}/build" --parallel "$JOBS"

APPDIR="${ROOT}/AppDir"
rm -rf "$APPDIR"
cmake --install "${ROOT}/build" --prefix "${APPDIR}/usr"

ICON="${APPDIR}/usr/share/icons/hicolor/256x256/apps/top.suhasdissa.QuickShareLinux.png"
mkdir -p "$(dirname "$ICON")"
rsvg-convert -w 256 -h 256 \
  "${ROOT}/packaging/top.suhasdissa.QuickShareLinux.svg" \
  -o "$ICON"

DEPLOY="${DEPS}/linuxdeploy"
mkdir -p "$DEPLOY"
# Always refresh. These are the upstream "continuous" builds, and a cached copy
# would stick around inside the dependency cache.
curl -fL --retry 3 --retry-delay 2 -o "${DEPLOY}/linuxdeploy.AppImage" \
  "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
curl -fL --retry 3 --retry-delay 2 -o "${DEPLOY}/linuxdeploy-plugin-qt.AppImage" \
  "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
chmod +x "${DEPLOY}/linuxdeploy.AppImage" "${DEPLOY}/linuxdeploy-plugin-qt.AppImage"

OUTPUT="QuickShareLinux-${VERSION}-x86_64.AppImage"
(
  cd "$DEPLOY"
  export APPIMAGE_EXTRACT_AND_RUN=1
  export QMAKE="${QT_ROOT_DIR}/bin/qmake"
  export VERSION
  export QML_SOURCES_PATHS="${ROOT}/ui/qml"
  export EXTRA_PLATFORM_PLUGINS="libqwayland.so"
  export EXTRA_QT_MODULES="svg"
  export ARCH=x86_64
  export LDAI_OUTPUT="$OUTPUT"
  if [[ -n "${GITHUB_REPOSITORY:-}" ]]; then
    export LDAI_UPDATE_INFORMATION="gh-releases-zsync|${GITHUB_REPOSITORY/\//|}|latest|QuickShareLinux-*-x86_64.AppImage.zsync"
  fi
  ./linuxdeploy.AppImage --appdir "$APPDIR" --plugin qt --output appimage \
    -d "${ROOT}/packaging/top.suhasdissa.QuickShareLinux.desktop" \
    -i "$ICON" \
    -e "${APPDIR}/usr/bin/quickshare"
)
mv "${DEPLOY}/${OUTPUT}" "${DIST}/${OUTPUT}"
if [[ -f "${DEPLOY}/${OUTPUT}.zsync" ]]; then
  mv "${DEPLOY}/${OUTPUT}.zsync" "${DIST}/${OUTPUT}.zsync"
fi
test -s "${DIST}/${OUTPUT}"
