#!/bin/sh
# Start a headless sway (software rendering, no input devices) and export
# WAYLAND_DISPLAY / SWAYSOCK for the caller. Source this file.
#   OUT_W x OUT_H at OUT_SCALE (logical = physical / scale)
: "${OUT_W:=2400}" "${OUT_H:=1500}" "${OUT_SCALE:=1.5}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/xdg-$(id -u)}"
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
conf="$XDG_RUNTIME_DIR/sway.conf"
cat > "$conf" <<CONF
output HEADLESS-1 resolution ${OUT_W}x${OUT_H} scale ${OUT_SCALE} bg #11111b solid_color
default_border none
default_floating_border none
gaps inner 0
seat * hide_cursor 1
focus_follows_mouse no
CONF
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 \
  sway -c "$conf" > "$XDG_RUNTIME_DIR/sway.log" 2>&1 &
SWAY_PID=$!
i=0
while [ -z "$(ls "$XDG_RUNTIME_DIR"/wayland-* 2>/dev/null | grep -v lock | head -1)" ]; do
  i=$((i + 1)); [ $i -gt 100 ] && { cat "$XDG_RUNTIME_DIR/sway.log"; exit 1; }; sleep 0.1
done
export WAYLAND_DISPLAY="$(basename "$(ls "$XDG_RUNTIME_DIR"/wayland-* | grep -v lock | head -1)")"
export SWAYSOCK="$(ls "$XDG_RUNTIME_DIR"/sway-ipc.*.sock | head -1)"
export QT_QPA_PLATFORM=wayland
echo "sway up: $WAYLAND_DISPLAY $SWAYSOCK"
