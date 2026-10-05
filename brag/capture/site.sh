#!/bin/sh
# Stills and short loops of the real HyprFM for the website (hyprfm-site).
# Usage: site.sh OUT BIN
# Each still is taken after the preview has settled (no "Loading preview…").
set -eux
OUT="$1"; BIN="$2"; mkdir -p "$OUT/stills" "$OUT/loops"
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/session.sh"
. "$HERE/lib.sh"
trap 'cp "$XDG_RUNTIME_DIR/sway.log" "$OUT/" 2>/dev/null; kill $(jobs -p) 2>/dev/null; kill $SWAY_PID 2>/dev/null; exit 0' EXIT
H="$HOME"

# shot NAME [settle]: send the queued keys, wait for previews to settle, grab
shot() { play; sleep "${2:-2.2}"; timeout 10 grim "$OUT/stills/$1.png"; }
# loop NAME: record whatever the queued keys do
loop() { TAKE="$1"; rec_start "loop-$1"; play; rec_stop 0.6; mv "$OUT/loop-$1.mp4" "$OUT/loops/$1.raw.mp4"; }

with_actions() {   # a custom context action, to show the menu entry and its shortcut
  cat >> "$CFG" <<'CONF'

[[context_menu.actions]]
name = "Rate my rice"
command = "notify-send 'rice level' 'over 9000'"
types = ["dir", "image/*"]
shortcut = "Ctrl+R"

[[context_menu.actions]]
name = "Set as wallpaper (swww)"
command = "swww img %f"
types = ["image/*"]
CONF
}

# --- hero: Miller through every wallpaper, then quick preview through Pictures
write_config miller; app_start "$H"
t pic 0.3; k Right 0.4; t wal 0.3; k Right 0.2; shot hero-w0
for i in 1 2 3 4 5 6; do k Down 0.1; shot "hero-w$i"; done
app_stop

write_config grid; app_start "$H/Pictures"
t cod 0.4; k space 0.2; shot peek-0 2.6
for i in 1 2 3 4 5; do k Right 0.1; shot "peek-$i" 2.4; done
app_stop

# --- every preview type, inline in Miller
write_config miller; app_start "$H"
t cod 0.3; k Right 0.4; k Right 0.4
for f in Cargo.toml main.rs rice.py style.css README.md; do t "$f" 0.1; shot "prev-code-$f"; done
k Left 0.3; k Left 0.3; t doc 0.3; k Right 0.3
for f in todo.md thesis-final rice-budget excuses hyprland.conf; do t "$f" 0.1; shot "prev-doc-$f" 2.6; done
k Left 0.3; t dow 0.3; k Right 0.3
for f in dotfiles hyprland-0 definitely; do t "$f" 0.1; shot "prev-dl-$f" 2.4; done
k Left 0.3; t fon 0.3; k Right 0.1; shot prev-font-0 2.4; k Down 0.1; shot prev-font-1 2.4
k Left 0.3; t mus 0.3; k Right 0.1; shot prev-music 2.4
k Left 0.3; t vid 0.3; k Right 0.1; shot prev-video 2.8
k Left 0.3; t cod 0.3; k Right 0.1; shot prev-folder 1.6
app_stop

# --- quick preview on media and documents
write_config grid; app_start "$H/Music"
t lof 0.3; k space 0.2; shot qp-music 2.4; k Escape 0.4
c alt Up 0.5; t vid 0.3; k Return 0.6; t lau 0.3; k space 0.2; shot qp-video 2.8; k Escape 0.4
c alt Up 0.5; t doc 0.3; k Return 0.6; t thesis-f 0.3; k space 0.2; shot qp-pdf-1 2.6
k Down 0.1; shot qp-pdf-2 2.0; k Down 0.1; shot qp-pdf-3 2.0; k Escape 0.4
t todo 0.3; k space 0.2; shot qp-md 2.2; k Escape 0.4
app_stop

# --- the three views of one folder
write_config grid; app_start "$H/Pictures/Wallpapers"
t gre 0.2; shot view-grid 2.4
c ctrl 3 0.2; shot view-detailed 1.8
c ctrl 2 0.2; shot view-miller 2.4
app_stop

# --- tabs, search, path bar
write_config grid; app_start "$H"
c ctrl t 0.4; t pic 0.3; k Return 0.6; c ctrl t 0.4; t cod 0.3; k Return 0.6; c ctrl t 0.4; t doc 0.3; k Return 0.2; shot tabs 1.8
app_stop
write_config grid; app_start "$H"
c ctrl f 0.4; t jpg 0.2; shot search 3.0
app_stop
write_config grid; app_start "$H"
c ctrl l 0.4; t "~/Pi" 0.2; shot pathbar 1.2
app_stop

# --- split view with a copy across
write_config grid; app_start "$H/Pictures/Wallpapers"
k F3 1.2; c ctrl+alt Right 0.5; c alt Up 0.8; t scr 0.4; k Return 1.0
c ctrl+alt Left 0.6; t gre 0.6; c ctrl c 0.6; c ctrl+alt Right 0.6; c ctrl v 0.2; shot split 2.0
app_stop

# --- bulk rename, live preview
write_config grid; app_start "$H/Downloads"
t wallpaper 0.6; c shift Right 0.6; k F2 1.2; t wallpaper 0.2; shot rename 1.4
app_stop

# --- context menu with custom actions, shortcuts dialog, settings
write_config grid; with_actions; app_start "$H/Pictures"
t pair 0.4; c shift F10 0.2; shot context-menu 1.4
app_stop
write_config grid; app_start "$H"
c ctrl+shift slash 0.6; shot shortcuts 1.2; t rename 0.2; shot shortcuts-search 1.2
app_stop
write_config grid; app_start "$H"
c ctrl comma 0.2; shot settings 2.4
app_stop

# --- every bundled theme on the same view
write_config miller; app_start "$H"
t pic 0.3; k Right 0.4; t wal 0.3; k Right 0.4; t cos 0.2; play; sleep 2.4
for th in catppuccin-mocha catppuccin-latte rose-pine rose-pine-moon rose-pine-dawn nord dracula gruvbox-dark gruvbox-light monokai-pro; do
  sed -i "s/^theme = .*/theme = \"$th\"/" "$CFG"; sleep 1.6
  timeout 10 grim "$OUT/stills/theme-$th.png"
done
app_stop

# --- short loops for the feature sections
write_config miller; app_start "$H"
t pic 0.3; k Right 0.4; t wal 0.3; k Right 0.2; play; sleep 1.5
for i in 1 2 3 4 5 6; do k Down 1.4; done; loop miller
app_stop
write_config grid; app_start "$H/Pictures"
t cod 0.4; play; sleep 1
k space 2.0; k Right 1.6; k Right 1.6; k Right 1.6; k Escape 0.6; loop peek
app_stop
write_config grid; app_start "$H/Pictures/Wallpapers"
k F3 1.2; c ctrl+alt Right 0.5; c alt Up 0.8; t scr 0.4; k Return 1.0
c ctrl+alt Left 0.6; t gre 0.6; c ctrl c 0.6; c ctrl+alt Right 0.6; c ctrl v 2.2; loop split
app_stop
write_config grid; app_start "$H/Documents"
t thesis-f 1.0; k Delete 2.0; c ctrl z 2.4; loop undo
app_stop

# --- encode for the web: stills to WebP, loops to small H.264 + WebM
for f in "$OUT"/stills/*.png; do
  magick "$f" -resize 2000x -quality 80 "${f%.png}.webp"
done
for f in "$OUT"/loops/*.raw.mp4; do
  n="${f%.raw.mp4}"
  ffmpeg -nostdin -loglevel error -y -i "$f" -vf "scale=1600:-2,fps=30" -c:v libx264 -crf 26 -preset slow -pix_fmt yuv420p -movflags +faststart -an "$n.mp4"
  ffmpeg -nostdin -loglevel error -y -i "$f" -vf "scale=1600:-2,fps=30" -c:v libvpx-vp9 -crf 38 -b:v 0 -an "$n.webm" || true
  ffmpeg -nostdin -loglevel error -y -i "$f" -frames:v 1 -vf scale=1600:-2 -q:v 3 "$n.jpg"
  rm "$f"
done
rm -f "$OUT"/stills/*.png
ls -la "$OUT/stills" "$OUT/loops"
