#!/bin/sh
# Build both cuts from the recorded takes and render them with HyperFrames.
#   render.sh TAKES_DIR OUT_DIR
set -eux
TAKES="$1"; OUT="$2"; HERE=$(cd "$(dirname "$0")/../composition" && pwd)
mkdir -p "$OUT" /tmp/hfdeps && cd /tmp/hfdeps
npm init -y >/dev/null
npm i --no-audit --no-fund gsap@3.14.2 @fontsource/instrument-serif @fontsource/inter @fontsource/jetbrains-mono
F=/tmp/hfdeps/node_modules/@fontsource
npx -y hyperframes browser ensure
for cut in ${CUTS:-long short}; do
  P="$OUT/$cut"
  node "$HERE/build.mjs" "$cut" "$TAKES" "$P"
  mkdir -p "$P/assets/fonts"
  cp /tmp/hfdeps/node_modules/gsap/dist/gsap.min.js "$P/assets/"
  cp $F/instrument-serif/files/instrument-serif-latin-400-normal.woff2 "$P/assets/fonts/instrument-serif-400.woff2"
  cp $F/instrument-serif/files/instrument-serif-latin-400-italic.woff2 "$P/assets/fonts/instrument-serif-400-italic.woff2"
  cp $F/inter/files/inter-latin-500-normal.woff2 "$P/assets/fonts/inter-500.woff2"
  cp $F/inter/files/inter-latin-800-normal.woff2 "$P/assets/fonts/inter-800.woff2"
  cp $F/jetbrains-mono/files/jetbrains-mono-latin-600-normal.woff2 "$P/assets/fonts/jetbrains-mono-600.woff2"
  ( cd "$P" && npx -y hyperframes check > "$OUT/$cut-check.txt" 2>&1 ) || echo "CHECK FAILED: $cut" | tee -a "$OUT/failed.txt"
  ( cd "$P" && npx -y hyperframes render --quality ${QUALITY:-delivery} --output "$OUT/hyprfm-$cut.mp4" > "$OUT/$cut-render.txt" 2>&1 ) || echo "RENDER FAILED: $cut" | tee -a "$OUT/failed.txt"
  # review frames: one every 2 s
  [ -s "$OUT/hyprfm-$cut.mp4" ] && ffmpeg -nostdin -loglevel error -y -i "$OUT/hyprfm-$cut.mp4" -vf "fps=0.5,scale=640:-2,tile=4x12:margin=4:padding=4" -frames:v 1 "$OUT/sheet-$cut.jpg" || true
  cp "$P/index.html" "$OUT/$cut-index.html"; cp "$P/timeline.json" "$OUT/$cut-timeline.json"
done
