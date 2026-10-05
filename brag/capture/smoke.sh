#!/bin/sh
# Smoke test: can we drive and record HyprFM in headless sway on CI?
set -eux
OUT="$1"; BIN="$2"; mkdir -p "$OUT"
. "$(dirname "$0")/session.sh"
trap 'cp "$XDG_RUNTIME_DIR/sway.log" "$OUT/" 2>/dev/null; kill $(jobs -p) 2>/dev/null; kill $SWAY_PID 2>/dev/null; true' EXIT
mkdir -p ~/Pictures ~/Documents ~/Code; echo 'fn main() { println!("hi"); }' > ~/Code/main.rs
"$BIN" ~ > "$OUT/hyprfm.log" 2>&1 &
sleep 4
timeout 5 swaymsg -t get_tree | grep -o '"app_id": "[^"]*"' | sort -u
timeout 10 grim "$OUT/still-home.png"
wf-recorder -o HEADLESS-1 -c libx264 -p preset=veryfast -p crf=18 -f "$OUT/clip.mp4" > "$OUT/wfr.log" 2>&1 &
REC=$!
sleep 1
timeout 5 wtype -M ctrl -k 2 -m ctrl; sleep 1
for k in Down Down Right Down; do timeout 5 wtype -k $k; sleep 0.7; done
timeout 5 wtype -k space; sleep 1.5; timeout 5 wtype -k Escape; sleep 1
kill -INT $REC; timeout 15 sh -c "while kill -0 $REC 2>/dev/null; do sleep 0.2; done" || kill -9 $REC
timeout 10 grim "$OUT/still-end.png"
ffprobe -v error -show_entries stream=width,height,nb_frames,avg_frame_rate:format=duration -of compact "$OUT/clip.mp4" || true
