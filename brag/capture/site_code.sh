#!/bin/sh
# Quick preview of the two code files in ~/Code, for the website.
# Also compiles and runs them, so the code on the site is code that works.
set -eux
OUT="$1"; BIN="$2"; mkdir -p "$OUT/stills"
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/session.sh"
. "$HERE/lib.sh"
trap 'cp "$XDG_RUNTIME_DIR/sway.log" "$OUT/" 2>/dev/null; kill $(jobs -p) 2>/dev/null; kill $SWAY_PID 2>/dev/null; exit 0' EXIT
H="$HOME"
{ python3 "$H/Code/deadline.py"; rustc -O "$H/Code/optimal_gaps.rs" -o /tmp/og && /tmp/og; } > "$OUT/code-run.txt" 2>&1 || true

shot() { play; sleep "${2:-2.2}"; timeout 10 grim "$OUT/stills/$1.png"; }
write_config grid; app_start "$H/Code"
t opt 0.4; k space 0.2; shot qp-code-rs 2.6; k Escape 0.5
t dea 0.4; k space 0.2; shot qp-code-py 2.6; k Escape 0.5
app_stop
for f in "$OUT"/stills/*.png; do magick "$f" -resize 2000x -quality 82 "${f%.png}.webp"; rm "$f"; done
ls -la "$OUT/stills"; cat "$OUT/code-run.txt"
