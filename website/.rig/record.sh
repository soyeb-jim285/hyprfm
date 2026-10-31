#!/usr/bin/env bash
# record.sh <name> <setup-dir> <actions...>
# Slow-motion capture: HyprFM and Hyprland animations run SLOW times slower, grim samples
# as fast as it can, and the encode retimes each frame by its real timestamp / SLOW.
cd "$(dirname "$0")"; R=$PWD; source ./session.env
export HYPRLAND_INSTANCE_SIGNATURE=$SIG SLOW=${SLOW:-4}
name=$1; dir=$2; shift 2
k(){ ./app.sh key "$1" >/dev/null; sleep $(echo "${2:-0.6} * $SLOW" | bc); }
t(){ ./app.sh type "$1"; }
w(){ sleep $(echo "$1 * $SLOW" | bc); }
park(){ ./app.sh hc dispatch "hl.dsp.cursor.move({ x = 7590, y = 990 })" >/dev/null; }
./app.sh kill; THEME=$THEME ./make-home.sh >/dev/null 2>&1
s=$(echo "4 / $SLOW" | bc -l); ./app.sh hc eval "hl.animation({ leaf = 'windows', enabled = true, speed = $s, bezier = 'out', style = 'popin 94%' }) hl.animation({ leaf = 'fade', enabled = true, speed = $s, bezier = 'out' })" >/dev/null
[ -n "$PRE" ] || { ./app.sh fm "$dir"; sleep 7; park; }
[ -n "$PRE" ] && eval "$PRE"
F=$R/run/frames/$name; rm -rf "$F"; mkdir -p "$F"
( export XDG_RUNTIME_DIR=$R/run WAYLAND_DISPLAY=$WL; while [ ! -e "$F/stop" ]; do grim -o CAP -t jpeg -q 94 "$F/$(date +%s%N).jpg"; done ) &
cap=$!
w 0.5
for a in "$@"; do eval "$a"; done
w 0.6
touch "$F/stop"; wait $cap
ls "$F"/*.jpg | sort > "$F/list"
awk -v slow=$SLOW '{ split($0,p,"/"); ts=p[length(p)]; sub(/\.jpg/,"",ts); f[NR]=$0; t[NR]=ts } END { for (i=1;i<NR;i++) { printf "file '\''%s'\''\nduration %.6f\n", f[i], (t[i+1]-t[i])/1e9/slow }; printf "file '\''%s'\''\nduration 0.1\nfile '\''%s'\''\n", f[NR], f[NR] }' "$F/list" > "$F/concat.txt"
n=$(wc -l < "$F/list"); real=$(( ($(tail -1 "$F/list" | sed 's|.*/||; s|\.jpg||') - $(head -1 "$F/list" | sed 's|.*/||; s|\.jpg||')) / 1000000 ))
mkdir -p ../media/clips
ffmpeg -loglevel error -y -f concat -safe 0 -i "$F/concat.txt" -vf "fps=60,format=yuv420p" -c:v libx264 -crf 12 -preset slow ../media/clips/$name.mp4
echo "$name: $n frames over ${real} ms real -> $(echo "$n * 1000 * $SLOW / $real" | bc) fps effective"
./app.sh hc eval "hl.animation({ leaf = 'windows', enabled = true, speed = 4, bezier = 'out', style = 'popin 94%' }) hl.animation({ leaf = 'fade', enabled = true, speed = 4, bezier = 'out' })" >/dev/null
