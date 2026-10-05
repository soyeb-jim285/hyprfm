#!/bin/sh
# Lay the music bed under a rendered cut (which already carries the CC0 key
# sounds). The music is not committed to this public repo; it comes from the
# /brag skill's bundled library on the machine that runs this.
#   mux-music.sh IN.mp4 MUSIC.mp3 OUT.mp4 [music_volume]
set -eu
IN="$1"; MUSIC="$2"; OUT="$3"; VOL="${4:-0.30}"
D=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$IN")
FO=$(awk -v d="$D" 'BEGIN{printf "%.3f", d-2.5}')
ffmpeg -nostdin -loglevel error -y -i "$IN" -i "$MUSIC" -filter_complex \
  "[1:a]atrim=0:$D,asetpts=PTS-STARTPTS,volume=$VOL,afade=t=in:d=1.0,afade=t=out:st=$FO:d=2.5[m];\
   [0:a][m]amix=inputs=2:duration=first:normalize=0,alimiter=limit=0.89[a]" \
  -map 0:v -map "[a]" -c:v copy -c:a aac -b:a 256k -movflags +faststart "$OUT"
