#!/bin/sh
# Usage: fetch.sh TARGET_ROOT   (reads manifest.tsv next to this script)
set -eu
ROOT=${1:?usage: fetch.sh TARGET_ROOT}
HERE=$(cd "$(dirname "$0")" && pwd)
UA="hyprfm-brag-demo/1.0 (github.com/soyeb-jim285/hyprfm)"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
TAB=$(printf '\t')

while IFS="$TAB" read -r dest url maxw title author page lic; do
  [ -n "$dest" ] || continue
  out="$ROOT/$dest"; mkdir -p "$(dirname "$out")"
  echo "==> $dest"
  raw="$TMP/raw"; rm -f "$raw"
  curl -fL --retry 3 --retry-delay 5 -A "$UA" -o "$raw" "$url" || { echo "DOWNLOAD FAILED: $url" >&2; exit 1; }
  case "$dest" in
    *.jpg)
      w=${maxw%+exif}   # +exif = photo whose EXIF must survive; we never -strip, so all do
      if [ "$w" != "-" ]; then
        magick "$raw" -resize "${w}x>" -quality 88 "$out"   # no -strip: EXIF kept
      else
        magick "$raw" -quality 88 "$out"
      fi ;;
    *.mp4)
      ffmpeg -y -loglevel error -i "$raw" -t 20 -vf "scale='min(1920,iw)':-2" \
        -c:v libx264 -crf 26 -an -movflags +faststart "$out" ;;
    *.mp3)
      case "$dest" in
        *gymnopedie*|*lofi*) date=1888 ;; *) date=1875 ;;
      esac
      ffmpeg -y -loglevel error -i "$raw" -t 180 -vn -map_metadata -1 -c:a libmp3lame -b:a 192k \
        -id3v2_version 3 -metadata title="$title" -metadata artist="$author" \
        -metadata album="Music To Rice To" -metadata genre="Classical" -metadata date="$date" "$out" ;;
    *) echo "unknown type: $dest" >&2; exit 1 ;;
  esac
  [ -s "$out" ] || { echo "EMPTY OUTPUT: $dest" >&2; exit 1; }
done < "$HERE/manifest.tsv"
echo "fetch done"
