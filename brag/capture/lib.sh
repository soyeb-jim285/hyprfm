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
  sleep 0.6; T0=$(now); sleep 0.4
}
rec_stop() {
  sleep "${1:-0.8}"
  timeout 10 grim -s 0.5 "$OUT/$TAKE-end.png" || true
  kill -INT "$REC"
  timeout 20 sh -c "while kill -0 $REC 2>/dev/null; do sleep 0.2; done" || kill -9 "$REC"
  LOG=""; T0=""
}

# Keys are queued and sent by ONE wtype process per sequence: every wtype run
# uploads a fresh keymap, and a key that arrives right behind a keymap change
# gets dropped or misread by the client. One run = one keymap upload.
# k KEY [sleep]           single key
# c MOD[+MOD] KEY [sleep] chord, e.g. c ctrl 2, c ctrl+alt Right
# t TEXT [sleep]          typed text (70 ms per character)
# play                    send the queue; logs "<seconds> <kind> <what>" to $LOG
Q=""; VT=0; EV=""
_q() { Q="$Q $1"; }
_adv() { VT=$(awk -v a="$VT" -v b="$1" 'BEGIN{printf "%.3f", a+b}'); }
_ev() { EV="$EV$VT $*
"; }
_sleep() { _q "-s $(awk -v s="$1" 'BEGIN{printf "%d", s*1000}')"; _adv "$1"; }
k() { _ev key "$1"; _q "-k $1"; _adv 0.03; _sleep "${2:-0.6}"; }
c() {
  for m in $(echo "$1" | tr '+' ' '); do _q "-M $m"; done
  _ev chord "$1+$2"; _q "-k $2"
  for m in $(echo "$1" | tr '+' ' '); do _q "-m $m"; done
  _adv 0.03; _sleep "${3:-0.6}"
}
t() {
  _ev type "$1"
  esc=$(printf '%s' "$1" | sed "s/'/'\\\\''/g")
  _q "'$esc'"
  _adv "$(awk -v n="${#1}" 'BEGIN{printf "%.3f", n*0.07}')"; _sleep "${2:-0.6}"
}
play() {
  [ -n "$Q" ] || return 0
  base=0
  [ -n "${T0:-}" ] && [ -n "${LOG:-}" ] && base=$(awk -v a="$(now)" -v b="$T0" 'BEGIN{printf "%.3f", a-b}')
  # Shift_L first: a throwaway key that absorbs the keymap switch.
  eval "timeout 120 wtype -d 70 -k Shift_L -s 300 $Q" || echo "wtype failed: $Q" >&2
  if [ -n "${LOG:-}" ]; then
    printf '%s' "$EV" | awk -v b="$base" 'NF{t=$1; $1=""; printf "%.3f%s\n", t+b+0.33, $0}' >> "$LOG"
  fi
  Q=""; VT=0; EV=""
}
set_theme() { echo "$(awk -v a="$(now)" -v b="$T0" 'BEGIN{printf "%.3f", a-b}') theme $1" >> "$LOG"; sed -i "s/^theme = .*/theme = \"$1\"/" "$CFG"; sleep "${2:-0.5}"; }
