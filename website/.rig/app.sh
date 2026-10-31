#!/usr/bin/env bash
# Usage: app.sh wall | fm [path] | grab <out.png> | key "<mods>,<key>" | hc <args...>
cd "$(dirname "$0")"; R=$PWD; source "$R/session.env"
export HYPRLAND_INSTANCE_SIGNATURE=$SIG
NEST="env XDG_RUNTIME_DIR=$R/run WAYLAND_DISPLAY=$WL"
case $1 in
  wall) $NEST awww-daemon >$R/run/awww.log 2>&1 & sleep 2; $NEST awww img --transition-type none "$R/../media/wall.png" ;;
  fm) shift
      bwrap --dev-bind / / --tmpfs /run/dbus --tmpfs /home --bind /home/jim /home/jim --bind "$R/home" /home/you --unshare-uts --hostname trail --chdir /home/you \
        env TZ=Europe/London LANG=en_GB.UTF-8 PS1="\\W \\$ " SHELL=/bin/bash HOME=/home/you USER=you LOGNAME=you XDG_CONFIG_HOME=/home/you/.config XDG_DATA_HOME=/home/you/.local/share \
            XDG_CACHE_HOME=/home/you/.cache XDG_RUNTIME_DIR=$R/run WAYLAND_DISPLAY=$WL QT_QPA_PLATFORM=wayland \
            QT_FORCE_STDERR_LOGGING=1 /home/jim/hyprfm/build/src/hyprfm "$@" >$R/run/fm.log 2>&1 &
      echo $! >$R/run/fm.pid ;;
  grab) $NEST grim -o CAP "$2" ;;
  key) hyprctl dispatch "hl.dsp.send_shortcut({ mods = '${2%%,*}', key = '${2#*,}', window = 'class:hyprfm' })" ;;
  hc) shift; hyprctl "$@" ;;
esac
case $1 in
  kill) p=$(cat "$R/run/fm.pid"); for c in $(pgrep -P $p); do kill $c; done; kill $p 2>/dev/null; sleep 1; rm -f "$R/home/.local/share/hyprfm/session.json" "$R/home/.config/hyprfm/session.json" ;;
esac
case $1 in
  type) s=$2; for ((i=0;i<${#s};i++)); do c=${s:i:1}; case "$c" in " ") c=space;; "-") c=minus;; ".") c=period;; "/") c=slash;; esac
          hyprctl dispatch "hl.dsp.send_shortcut({ mods = '', key = '$c', window = 'class:hyprfm' })" >/dev/null; sleep 0.05; done ;;
esac
