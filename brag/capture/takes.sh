#!/bin/sh
# Every take for the brag videos. Usage: takes.sh OUT BIN [take...]
# Each take starts HyprFM fresh in a known place, records, and logs keys.
set -eux
OUT="$1"; BIN="$2"; shift 2; mkdir -p "$OUT"
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/session.sh"
. "$HERE/lib.sh"
trap 'cp "$XDG_RUNTIME_DIR/sway.log" "$OUT/" 2>/dev/null; kill $(jobs -p) 2>/dev/null; kill $SWAY_PID 2>/dev/null; exit 0' EXIT
H="$HOME"

take_launch() {        # the window appearing, from an empty desktop
  write_config grid
  rec_start launch
  echo "0.4 launch" > "$LOG"; "$BIN" "$H" >> "$OUT/hyprfm.log" 2>&1 & APP=$!
  sleep 3
  play; rec_stop; app_stop
}

take_wallpapers() {    # Miller: scrub the preview through the wallpapers
  write_config miller; app_start "$H"
  rec_start wallpapers
  t pic 0.4; k Right 0.5; t wal 0.4; k Right 1.4
  for i in 1 2 3 4 5 6; do k Down 1.0; done
  play; rec_stop 0.6; app_stop
}

take_tour() {          # Miller: one folder of everything, previewed inline
  write_config miller; app_start "$H"
  rec_start tour
  t cod 0.4; k Right 0.5; k Right 0.6; t main 2.8; t rice 2.4
  k Left 0.3; k Left 0.4; t doc 0.4; k Right 0.5; t todo 2.8; t thesis-f 2.8
  k Left 0.4; t dow 0.4; k Right 0.5; t dot 2.8
  k Left 0.4; t fon 0.4; k Right 2.8
  play; rec_stop; app_stop
}

take_cold() {          # cold open: the ricer's todo list, held
  write_config miller; app_start "$H"
  t doc 0.4; k Right 0.5; t todo 0.4; play
  sleep 1.5
  rec_start cold
  sleep 7
  rec_stop 0.2; app_stop
}

take_peek() {          # Space quick preview over the pictures, then browse
  write_config grid; app_start "$H/Pictures"
  rec_start peek
  t pair 0.8; k space 2.6; k Right 1.6; k Right 1.6; k Right 1.6; k Right 1.6; k Escape 0.8
  play; rec_stop; app_stop
}

take_peek_media() {    # Space on music tags, a video poster, a PDF
  write_config grid; app_start "$H/Music"
  rec_start peek_media
  t lof 0.5; k space 2.6; k Escape 0.6
  c alt Up 0.6; t vid 0.3; k Return 0.8; t lau 0.5; k space 2.4; k Escape 0.6
  c alt Up 0.6; t doc 0.3; k Return 0.8; t thesis-f 0.5; k space 2.2; k Down 1.4; k Down 1.6; k Escape 0.6
  play; rec_stop; app_stop
}

take_split() {         # F3 split, copy a wallpaper across panes, no mouse
  write_config grid; app_start "$H/Pictures/Wallpapers"
  rec_start split
  k F3 1.2; c ctrl+alt Right 0.5; c alt Up 0.8; t scr 0.4; k Return 1.0
  c ctrl+alt Left 0.6; t gre 0.6; c ctrl c 0.6; c ctrl+alt Right 0.6; c ctrl v 2.0
  play; rec_stop; app_stop
}

take_rename() {        # bulk rename with a live preview
  write_config grid; app_start "$H/Downloads"
  rec_start rename
  t wallpaper 0.6; c shift Right 0.6; k F2 1.4
  t wallpaper 0.5; k Tab 0.4; t wallpaper-FINAL-v2 2.2; k Return 2.2
  play; rec_stop; app_stop
}

take_undo() {          # delete the thesis, regret it, Ctrl+Z
  write_config grid; app_start "$H/Documents"
  rec_start undo
  t thesis-f 1.0; k Delete 2.0; c ctrl z 2.4
  play; rec_stop; app_stop
}

take_themes() {        # live theme reload while browsing
  write_config miller; app_start "$H"
  t pic 0.3; k Right 0.4; t wal 0.3; k Right 0.6; t cos 1.2; play
  rec_start themes
  sleep 0.8
  for th in rose-pine nord gruvbox-dark dracula catppuccin-latte rose-pine-dawn monokai-pro rose-pine-moon gruvbox-light catppuccin-mocha; do
    set_theme "$th" 0.9
  done
  play; rec_stop; app_stop
}

take_git() {           # git status badges in the detailed view
  write_config detailed; app_start "$H/Code/rice-o-meter"
  rec_start git
  sleep 0.6; k Down 0.8; k Down 0.8; k Down 0.8; k Down 1.0
  play; rec_stop; app_stop
}

take_pathbar() {       # Ctrl+L, type a path, suggestions, Enter
  write_config grid; app_start "$H"
  rec_start pathbar
  c ctrl l 0.6; t "~/Pictures/Wa" 1.4; k Down 0.6; k Return 1.8
  play; rec_stop; app_stop
}

ALL="cold launch wallpapers tour peek peek_media split rename undo themes git pathbar"
for name in ${*:-$ALL}; do
  "take_$name" || echo "TAKE FAILED: $name" >> "$OUT/failed.txt"
done

# A frame every second from every take, for review without downloading video.
for f in "$OUT"/*.mp4; do
  n=$(basename "$f" .mp4)
  ffmpeg -nostdin -loglevel error -y -i "$f" -vf "fps=1,scale=800:-2,tile=4x6:margin=6:padding=6" -frames:v 1 "$OUT/sheet-$n.jpg" || true
  ffprobe -v error -show_entries format=duration:stream=nb_frames -of compact "$f" >> "$OUT/durations.txt" || true
done
