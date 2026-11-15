#!/bin/bash
# Drop what HyprFM never loads, and the libraries that must come from the host.
# Run after the last linuxdeploy call, then package with appimagetool.
# Every removal here is covered by the AppImage smoke test.
set -euo pipefail
appdir="${1:?usage: prune-appdir.sh <AppDir>}"

# QQuickStyle is hard-set to "Basic" in main.cpp; the other styles are dead weight.
for style in FluentWinUI3 Fusion Imagine Material Universal; do
    rm -rf "$appdir/usr/qml/QtQuick/Controls/$style"
    rm -f "$appdir"/usr/lib/libQt6QuickControls2${style}*.so*
done
rm -rf "$appdir/usr/qml/QtQuick/Controls/designer"

# No `import QtQuick.Dialogs` anywhere; Basic's dependency scan drags it in.
rm -rf "$appdir/usr/qml/QtQuick/Dialogs"
rm -f "$appdir"/usr/lib/libQt6QuickDialogs2*.so*

# Qt's own .qm files: HyprFM has no translations and never installs a QTranslator.
rm -rf "$appdir/usr/translations"

# Picked up from src/qml/Quill/Showcase.qml on machines that have Quickshell installed.
rm -rf "$appdir/usr/qml/Quickshell"

# Qt dlopens every image plugin the first time anything asks the clipboard
# for image formats (QQuickTextInput::q_canPasteChanged does, at startup),
# so each plugin here is paid for on every launch through the FUSE mount.
# These formats never get thumbnails in practice; jpeg/webp/gif/svg/tiff/ico stay.
for plugin in libqmng libqjp2 libqtga libqwbmp libqicns; do
    rm -f "$appdir/usr/plugins/imageformats/$plugin.so"
done
rm -f "$appdir"/usr/lib/libmng.so* "$appdir"/usr/lib/libjasper.so*

# Libraries that must come from the host. This is the only list: run this
# after the last linuxdeploy call and package with appimagetool, because any
# later linuxdeploy pass copies them back (it bundles the ldd closure, host
# libraries' dependencies included).
#
# - glib: GIO loads the host's gvfs modules, which need the host's glib; a
#   bundled 22.04 glib breaks trash, network places and mounts on newer distros.
# - glib's own dependencies, and dbus with its libsystemd: a bundled copy wins
#   the soname lookup under the host's newer libs. On Fedora 45 the host
#   libmount needs LIBSYSTEMD_251 and got the bundled 249 (#73).
# - xkbcommon: it parses the host's Compose files and must be as new as they
#   are (22.04's printed "unrecognized keysym dead_hamza" on Arch). Only the
#   core library: libxkbcommon-x11 stays bundled (a separate package on Arch).
# Every desktop distro the AppImage supports ships all of these.
host_libs=(
    glib-2.0 gobject-2.0 gio-2.0 gmodule-2.0 gthread-2.0
    mount blkid selinux pcre2-8 ffi
    dbus-1 systemd
    xkbcommon
)
for lib in "${host_libs[@]}"; do
    rm -f "$appdir/usr/lib/lib$lib.so"*
done
rm -rf "$appdir"/usr/share/doc/libglib2.0-0

# Then drop every library nothing in the AppDir links to any more. linuxdeploy
# bundles the whole ldd closure, so the dependencies of host libraries come
# along (libsystemd's libgcrypt, libxcb's libXau, ...): dead weight at best,
# and at worst an old copy that shadows the one the host library wants.
# Repeat until stable, since removing one can orphan the next. Qt's own
# libraries are exempt: QML plugins and Qt itself load some by name.
while :; do
    # readelf fails on the scripts and data files among them; that is fine.
    needed=$({ find "$appdir" -type f \( -name '*.so*' -o -path '*/bin/*' \) \
                   -exec readelf -d {} + 2>/dev/null || true; } \
             | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | sort -u)
    orphans=()
    for lib in "$appdir"/usr/lib/*.so*; do
        name=${lib##*/}
        [[ $name == libQt6* ]] && continue
        grep -qxF "$name" <<<"$needed" || orphans+=("$lib")
    done
    (( ${#orphans[@]} )) || break
    printf 'Unused, removed: %s\n' "${orphans[@]##*/}"
    rm -f "${orphans[@]}"
done

du -sh "$appdir" | sed 's/^/AppDir after prune: /'
