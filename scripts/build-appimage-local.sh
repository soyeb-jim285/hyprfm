#!/bin/bash
# Build a local AppImage from the current workspace state.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/appimage}"
APPDIR="${APPDIR:-$ROOT_DIR/build/AppDir}"
TOOLS_DIR="${TOOLS_DIR:-$ROOT_DIR/build/appimage-tools}"
USE_SYSTEM_NON_QT_LIBS="${USE_SYSTEM_NON_QT_LIBS:-0}"
SMOKE_TEST="${SMOKE_TEST:-1}"
JOBS="${JOBS:-2}"

require_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'Missing required tool: %s\n' "$1" >&2
        exit 1
    fi
}

download_file() {
    local url="$1"
    local dest="$2"

    if command -v curl >/dev/null 2>&1; then
        curl -L --fail -o "$dest" "$url"
        return
    fi

    if command -v wget >/dev/null 2>&1; then
        wget -O "$dest" "$url"
        return
    fi

    printf 'Need curl or wget to download %s\n' "$url" >&2
    exit 1
}

require_tool cmake
require_tool timeout
require_tool rg
require_tool sha256sum

QMAKE_BIN="$(command -v qmake6 || command -v qmake || true)"
if [[ -z "$QMAKE_BIN" ]]; then
    printf 'Missing required tool: qmake6 (or qmake)\n' >&2
    exit 1
fi

QT_PLUGIN_DIR="$($QMAKE_BIN -query QT_INSTALL_PLUGINS)"
QT_LIB_DIR="$($QMAKE_BIN -query QT_INSTALL_LIBS)"

mkdir -p "$TOOLS_DIR"

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"

if [[ ! -x "$LINUXDEPLOY" ]]; then
    download_file \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage" \
        "$LINUXDEPLOY"
    chmod +x "$LINUXDEPLOY"
fi

if [[ ! -x "$LINUXDEPLOY_QT" ]]; then
    download_file \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage" \
        "$LINUXDEPLOY_QT"
    chmod +x "$LINUXDEPLOY_QT"
fi

printf '%s  %s\n' \
    c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d "$LINUXDEPLOY" \
    15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724 "$LINUXDEPLOY_QT" |
    sha256sum -c -

# linuxdeploy discovers plugins by basename in its own directory.
ln -sf "$(basename "$LINUXDEPLOY_QT")" "$TOOLS_DIR/linuxdeploy-plugin-qt.AppImage"

VERSION_LABEL="$(git -C "$ROOT_DIR" describe --tags --always --dirty 2>/dev/null || printf 'local')"
VERSION_LABEL="$(printf '%s' "$VERSION_LABEL" | tr -cs 'A-Za-z0-9._-' '-')"
APPIMAGE_NAME="${APPIMAGE_NAME:-build/packages/HyprFM-${VERSION_LABEL}-x86_64.AppImage}"
mkdir -p "$ROOT_DIR/build/packages"

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
    GENERATOR_ARGS=(-G Ninja)
fi

rm -rf "$BUILD_DIR" "$APPDIR"
rm -f "$ROOT_DIR/$APPIMAGE_NAME"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" "${GENERATOR_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DHYPRFM_DATA_DIR=/usr/share/hyprfm \
    -DHYPRFM_ENABLE_QML_CACHEGEN=ON \
    -DOpenGL_GL_PREFERENCE=LEGACY \
    -DBUILD_TESTS=OFF

cmake --build "$BUILD_DIR" --parallel "$JOBS"

DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"
install -m755 "$ROOT_DIR/dist/AppRun" "$APPDIR/AppRun"

export QMAKE="$QMAKE_BIN"
export QML_SOURCES_PATHS="$ROOT_DIR/src/qml"
export NO_STRIP="${NO_STRIP:-1}"
export OUTPUT="$APPIMAGE_NAME"
export PATH="$TOOLS_DIR:$PATH"

# glib comes from the host: GIO loads the host's gvfs modules, which need the
# host's glib (a bundled 22.04 glib makes them fail with undefined symbols on
# newer distros, breaking trash, network places and mounts). Every distro the
# AppImage supports has glib, and newer ones are backward compatible.
excluded_libraries=(
    'libglib-2.0.so.*'
    'libgobject-2.0.so.*'
    'libgio-2.0.so.*'
    'libgmodule-2.0.so.*'
    'libgthread-2.0.so.*'
)

if [[ "$USE_SYSTEM_NON_QT_LIBS" == "1" ]]; then
    # linuxdeploy's patched copies of modern Arch system libraries can crash
    # before main() (e.g. libleancrypto via the media/poppler stack). For a
    # local test build, prefer bundling Qt and using host non-Qt libraries.
    excluded_libraries+=(
        'libavformat.so.*'
        'libavcodec.so.*'
        'libavutil.so.*'
        'libswresample.so.*'
        'libpoppler.so.*'
        'libpoppler-qt6.so.*'
        'libexif.so.*'
        'libtag.so.*'
        'libmount.so.*'
        'libffi.so.*'
        'libproxy.so.*'
        'liburing.so.*'
        'libcurl.so.*'
        'libgnutls.so.*'
        'libssl.so.*'
        'libcrypto.so.*'
        'libnss3.so*'
        'libnssutil3.so*'
        'libsmime3.so*'
        'libplc4.so*'
        'libplds4.so*'
        'libnspr4.so*'
        'libleancrypto.so.*'
        'libnghttp2.so.*'
        'libnghttp3.so.*'
        'libngtcp2*.so.*'
        'libssh.so.*'
        'libssh2.so.*'
        'libpsl.so.*'
        'libidn2.so.*'
        'libunistring.so.*'
        'libtasn1.so.*'
        'libhogweed.so.*'
        'libnettle.so.*'
        'libgpgme*.so.*'
        'libassuan.so.*'
        'libp11-kit.so.*'
    )

    printf 'Using host non-Qt libraries for local build.\n'
fi
LINUXDEPLOY_EXCLUDED_LIBRARIES="$(IFS=';'; printf '%s' "${excluded_libraries[*]}")"
export LINUXDEPLOY_EXCLUDED_LIBRARIES

platform_plugins=()
for plugin in \
    libqoffscreen.so \
    libqminimal.so \
    libqwayland.so \
    libqwayland-generic.so \
    libqwayland-egl.so
do
    if [[ -f "$QT_PLUGIN_DIR/platforms/$plugin" ]]; then
        platform_plugins+=("$plugin")
    fi
done

if (( ${#platform_plugins[@]} > 0 )); then
    EXTRA_PLATFORM_PLUGINS="$(IFS=';'; printf '%s' "${platform_plugins[*]}")"
    export EXTRA_PLATFORM_PLUGINS
    printf 'Bundling extra platform plugins: %s\n' "$EXTRA_PLATFORM_PLUGINS"
else
    unset EXTRA_PLATFORM_PLUGINS
fi

pushd "$ROOT_DIR" >/dev/null
LD_LIBRARY_PATH="$QT_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --plugin qt \
    --desktop-file "$ROOT_DIR/dist/io.github.soyeb_jim285.HyprFM.desktop" \
    --icon-file "$ROOT_DIR/dist/io.github.soyeb_jim285.HyprFM.svg"

deploy_deps_args=()
for plugin_dir in \
    wayland-graphics-integration-client \
    wayland-shell-integration \
    wayland-decoration-client
do
    if [[ -d "$QT_PLUGIN_DIR/$plugin_dir" ]]; then
        mkdir -p "$APPDIR/usr/plugins"
        rm -rf "$APPDIR/usr/plugins/$plugin_dir"
        cp -a "$QT_PLUGIN_DIR/$plugin_dir" "$APPDIR/usr/plugins/"
        deploy_deps_args+=(--deploy-deps-only "$APPDIR/usr/plugins/$plugin_dir")
        printf 'Bundled extra Qt plugin dir: %s\n' "$plugin_dir"
    fi
done

"$ROOT_DIR/scripts/prune-appdir.sh" "$APPDIR"

LD_LIBRARY_PATH="$QT_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    "${deploy_deps_args[@]}" \
    --output appimage

if [[ "$SMOKE_TEST" == "1" ]]; then
    smoke_dir="$(mktemp -d "$BUILD_DIR/smoke.XXXXXX")"
    set +e
    env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QML_IMPORT_PATH -u QML2_IMPORT_PATH \
        XDG_RUNTIME_DIR="$smoke_dir" XDG_CONFIG_HOME="$smoke_dir/config" \
        XDG_CACHE_HOME="$smoke_dir/cache" \
        QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        timeout 10 "$ROOT_DIR/$APPIMAGE_NAME" \
        > "$BUILD_DIR/smoke.stdout" \
        2> "$BUILD_DIR/smoke.stderr"
    smoke_status=$?
    set -e
    rm -rf "$smoke_dir"

    if (( smoke_status != 124 )); then
        printf 'AppImage smoke test exited with status %s\n' "$smoke_status" >&2
        cat "$BUILD_DIR/smoke.stderr" >&2 || true
        exit 1
    fi

    if rg -q "is not a type|failed to load component|QQmlApplicationEngine failed|TypeError:|Unable to assign|Segmentation fault|Aborted \(core dumped\)|Failed to initialize graphics backend|Failed to create RHI" "$BUILD_DIR/smoke.stderr"; then
        printf 'AppImage smoke test failed.\n' >&2
        cat "$BUILD_DIR/smoke.stderr" >&2 || true
        exit 1
    fi
fi

popd >/dev/null

printf 'Built AppImage: %s\n' "$ROOT_DIR/$APPIMAGE_NAME"

if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$ROOT_DIR/$APPIMAGE_NAME"
fi
