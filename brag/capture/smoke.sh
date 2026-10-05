#!/bin/sh
# Smoke test: can we drive and record HyprFM in headless sway on CI?
set -eu
OUT="$1"; BIN="$2"; mkdir -p "$OUT"
. "$(dirname "$0")/session.sh"
mkdir -p ~/Pictures ~/Documents ~/Code; echo 'fn main() { println!("hi"); }' > ~/Code/main.rs
"$BIN" ~ > "$OUT/hyprfm.log" 2>&1 &
sleep 4
swaymsg -t get_tree | grep -o '"app_id": "[^"]*"' | sort -u
grim "$OUT/still-home.png"
wf-recorder -o HEADLESS-1 -c libx264 -p preset=veryfast -p crf=18 -f "$OUT/clip.mp4" > "$OUT/wfr.log" 2>&1 &
REC=$!
sleep 1
wtype -M ctrl -k 2 -m ctrl; sleep 1
for k in Down Down Right Down; do wtype -k $k; sleep 0.7; done
wtype -k space; sleep 1.5; wtype -k Escape; sleep 1
kill -INT $REC; wait $REC || true
grim "$OUT/still-end.png"
ffprobe -v error -show_entries stream=width,height,nb_frames,avg_frame_rate:format=duration -of compact "$OUT/clip.mp4" || true
