#!/bin/sh
# Arch container setup for the brag-video pipeline: build deps, a headless
# Wayland session (sway + wtype + wf-recorder + grim), HyprFM's runtime
# preview tools, fonts/icons, and the media tools the asset script uses.
set -eu
pacman -Syu --noconfirm --needed \
  base-devel cmake ninja git \
  qt6-base qt6-declarative qt6-svg qt6-wayland qt6-multimedia kwindowsystem glib2 gvfs xdg-utils \
  sway wtype wf-recorder grim dbus \
  bat md4c ffmpeg poppler perl-image-exiftool fd wl-clipboard \
  imagemagick chromium nodejs npm python \
  adwaita-fonts noto-fonts noto-fonts-emoji inter-font ttf-jetbrains-mono ttf-fira-code papirus-icon-theme
useradd -m -s /bin/bash ricer 2>/dev/null || true
