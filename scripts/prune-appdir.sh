#!/bin/bash
# Drop Qt payload linuxdeploy-plugin-qt bundles but HyprFM never loads.
# Run after the first linuxdeploy pass, before `--output appimage`.
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

# glib comes from the host (see LINUXDEPLOY_EXCLUDED_LIBRARIES in
# build-appimage-local.sh). linuxdeploy-plugin-qt deploys without that list
# and copies libglib in anyway; a bundled 22.04 glib under the host's newer
# libgobject/gvfs fails with undefined symbols on current distros.
rm -f "$appdir"/usr/lib/lib{glib,gobject,gio,gmodule,gthread}-2.0.so*
rm -rf "$appdir"/usr/share/doc/libglib2.0-0

du -sh "$appdir" | sed 's/^/AppDir after prune: /'
