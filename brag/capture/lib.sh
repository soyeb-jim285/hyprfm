#!/bin/sh
# Helpers for scripted takes: start/stop HyprFM, record, and log every key
# with its offset from the start of the recording (the composition uses the
# log to place keycap overlays and keypress sounds on the real presses).
export PATH="$PATH:/usr/bin/vendor_perl"     # exiftool lives here on Arch
CFG="$HOME/.config/hyprfm/config.toml"

write_config() {   # $1 = default view, $2 = theme
  mkdir -p "$HOME/.config/hyprfm"
  cat > "$CFG" <<CONF
[general]
theme = "${2:-catppuccin-mocha}"
icon_theme = "Papirus-Dark"
font_family = "Adwaita Sans"
default_view = "${1:-miller}"
show_hidden = false
dependency_startup_check = false

[appearance]
transparency_enabled = false
radius_large = 12

[miller_view]
parent_fraction = 0.2
current_fraction = 0.3
CONF
}

app_start() {   # $1 = path to open
  rm -f "$HOME/.config/hyprfm/session.json"
  "$BIN" "$1" >> "$OUT/hyprfm.log" 2>&1 &
  APP=$!
  sleep "${APP_WAIT:-4}"
}
app_stop() { kill "$APP" 2>/dev/null; sleep 1.5; kill -9 "$APP" 2>/dev/null; true; }

now() { date +%s.%N; }
rec_start() {   # $1 = take name
  TAKE="$1"; LOG="$OUT/$TAKE.keys"; : > "$LOG"
  wf-recorder -D -o HEADLESS-1 -r 30 -c libx264 -p preset=veryfast -p crf=16 \
    -f "$OUT/$TAKE.mp4" > "$OUT/$TAKE.wfr.log" 2>&1 &
  REC=$!
  sleep 0.6; T0=$(now); sleep 0.6
}
rec_stop() {
  sleep "${1:-0.8}"
  timeout 10 grim -s 0.5 "$OUT/$TAKE-end.png" || true
  kill -INT "$REC"
  timeout 20 sh -c "while kill -0 $REC 2>/dev/null; do sleep 0.2; done" || kill -9 "$REC"
}
mark() { awk -v a="$(now)" -v b="$T0" -v m="$*" 'BEGIN{printf "%.3f %s\n", a-b, m}' >> "$LOG"; }
# k KEY [sleep]           single key            -> logged as "key KEY"
# c MOD[+MOD] KEY [sleep] chord, e.g. c ctrl 2   -> logged as "chord MOD+KEY"
# t TEXT [sleep]          typed text             -> logged as "type TEXT"
k() { mark key "$1"; timeout 5 wtype -k "$1"; sleep "${2:-0.6}"; }
c() {
  mods=$(echo "$1" | tr '+' ' '); press=""; release=""
  for m in $mods; do press="$press -M $m"; release="-m $m $release"; done
  mark chord "$1+$2"; timeout 5 wtype $press -k "$2" $release; sleep "${3:-0.6}"
}
t() { mark type "$1"; timeout 10 wtype -d 70 "$1"; sleep "${2:-0.6}"; }
set_theme() { mark theme "$1"; sed -i "s/^theme = .*/theme = \"$1\"/" "$CFG"; sleep "${2:-0.5}"; }
