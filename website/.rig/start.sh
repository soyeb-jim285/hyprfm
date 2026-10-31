#!/usr/bin/env bash
# Starts a nested Hyprland with a fixed-size headless output CAP, the fog wallpaper,
# and HyprFM in an isolated fake home. Writes ids to ./session.env.
set -e
cd "$(dirname "$0")"; R=$PWD
SCALE=${SCALE:-2}; W=${W:-1600}; H=${H:-1000}
RT=/run/user/1000
before=$(ls $RT/hypr; ls $RT | grep '^wayland-[0-9]*$')
env WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 HYPRLAND_INSTANCE_SIGNATURE= Hyprland -c $R/hypr.lua >$R/run/hypr.log 2>&1 &
HPID=$!
sleep 4
after=$(ls $RT/hypr; ls $RT | grep '^wayland-[0-9]*$')
new=$(comm -13 <(echo "$before" | sort) <(echo "$after" | sort))
SIG=$(echo "$new" | grep -v '^wayland' | head -1); WL=$(echo "$new" | grep '^wayland' | head -1)
[ -n "$SIG" ] && [ -n "$WL" ] || { echo "could not find new session: $new"; kill $HPID; exit 1; }
export HYPRLAND_INSTANCE_SIGNATURE=$SIG
hyprctl output create headless CAP >/dev/null
sleep 1
hyprctl eval "hl.monitor({ output = 'CAP', mode = '$((W*SCALE))x$((H*SCALE))@60', position = '6000x0', scale = $SCALE })" || true
hyprctl dispatch "hl.dsp.workspace.move_to_monitor({ workspace = '1', monitor = 'CAP' })" 2>/dev/null || true
cat > $R/session.env <<E
HPID=$HPID
SIG=$SIG
WL=$RT/$WL
E
echo "hyprland pid $HPID sig $SIG socket $WL"
hyprctl monitors | grep -E "^Monitor|scale|[0-9]+x[0-9]+@"
