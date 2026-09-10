#!/bin/sh
# Key overlay for screen recordings, colored from the active HyprFM theme.
#
#   ./scripts/showkeys.sh            # focused monitor
#   ./scripts/showkeys.sh eDP-1      # named monitor
#   Ctrl-C, or: pkill wshowkeys
#
# Needs the maintained fork — the original is archived and crashes on current
# Hyprland ("layerSurface was not configured, but a buffer was attached"):
#   yay -S wshowkeys-mao-rounded
#
# On a scaled output (anything but scale 1) the fork only repaints the top-left
# 1/scale of its surfaces, so a held Ctrl or Super highlights just the upper
# part of the cap. wshowkeys-damage-scale.patch next to this script fixes it;
# without it, record on an unscaled monitor.
set -eu

command -v wshowkeys >/dev/null || {
	echo "wshowkeys missing: yay -S wshowkeys-mao-rounded" >&2
	exit 1
}
wshowkeys -h 2>&1 | grep -q '\-M\]' || {
	echo "archived wshowkeys build — it crashes on Hyprland." >&2
	echo "replace it: yay -S wshowkeys-mao-git" >&2
	exit 1
}

# -r only exists in the wshowkeys-mao-rounded build, whose patch is currently
# stale against upstream. Square corners when it is missing.
round=''
wshowkeys -h 2>&1 | grep -q '\-r ' && round=1

conf="${XDG_CONFIG_HOME:-$HOME/.config}/hyprfm/config.toml"
setting() { sed -n "s/^$2[[:space:]]*=[[:space:]]*'\{0,1\}\"\{0,1\}\([^'\"]*\)['\"]\{0,1\}.*/\1/p" "$1" 2>/dev/null | head -1; }

theme="$(setting "$conf" theme)"
theme="${theme:-catppuccin-mocha}"
radius="$(setting "$conf" radius_large)"
radius="${radius:-12}"

# Prefer the checkout's themes over the installed copy, so editing a theme and
# rerunning shows the new colors.
here="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
for d in "$here/themes" /usr/share/hyprfm/themes; do
	[ -f "$d/$theme.toml" ] && { tfile="$d/$theme.toml"; break; }
done
[ -n "${tfile:-}" ] || { echo "theme not found: $theme.toml" >&2; exit 1; }

# Light or dark comes free: it is whichever theme HyprFM itself is running.
bg="$(setting "$tfile" base)e6"      # e6 = ~90% opaque over the wallpaper
fg="$(setting "$tfile" text)"
accent="$(setting "$tfile" accent)"  # modifiers, so Ctrl/Alt read distinct

mon="${1:-$(hyprctl -j monitors | jq -r '.[] | select(.focused) | .name')}"
hyprctl -j monitors | jq -e --arg m "$mon" 'any(.[]; .name == $m)' >/dev/null \
	|| { echo "no such monitor: $mon" >&2; exit 1; }

# wshowkeys' -o is a stub in every fork, but its layer surface lands on the
# focused monitor — so focus the target first. Recent Hyprland (0.56) turned dispatch
# into Lua and dropped the "focusmonitor <name>" string form, so try the old
# spelling and fall back to the Lua one.
hyprctl dispatch focusmonitor "$mon" >/dev/null 2>&1 \
	|| hyprctl repl "hl.dispatch(hl.dsp.focus{ monitor = '$mon' })" >/dev/null

echo "overlay on $mon — theme $theme, radius $radius"

# 48 is sized for a 2560-wide capture downscaled to 1080p — at 24 the caps were
# smaller than HyprFM's own status bar text. 4s so a shortcut is still on screen
# while the viewer looks at what it did.
font="${WSK_FONT:-Maple Mono NF 48}"

# The mao fork's -t is MILLISECONDS (original wshowkeys was seconds) — it
# compares -t against elapsed_ms with a 200ms default. Keep WSK_TIMEOUT in
# seconds and convert.
timeout_s="${WSK_TIMEOUT:-1}"
timeout=$(awk "BEGIN { print int($timeout_s * 1000) }")

# -M mods, -U mouse, -S scroll draw a row of key caps that sits on screen even
# when nothing is pressed. It is the only way Ctrl+Scroll reads on camera, but
# it is clutter in a shot that is only typing: WSK_BARE=1 shows keys alone.
mods='-M -U -S'
[ "${WSK_BARE:-0}" = 1 ] && mods=''

exec wshowkeys \
	-b "$bg" -f "$fg" -s "$accent" \
	-F "$font" \
	${round:+-r "$radius"} \
	-t "$timeout" -a bottom -m 80 $mods
