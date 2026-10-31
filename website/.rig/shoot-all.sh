#!/usr/bin/env bash
# Drives the sandboxed HyprFM through each state and grabs it.
cd "$(dirname "$0")"
k(){ ./app.sh key "$1" >/dev/null; sleep ${2:-0.6}; }
park(){ ./app.sh hc dispatch "hl.dsp.cursor.move({ x = 7590, y = 990 })" >/dev/null; }
open(){ ./app.sh kill; THEME=$THEME ./make-home.sh >/dev/null 2>&1; ./app.sh fm "$1"; sleep 6; park; }
want(){ [ -z "$ONLY" ] || [[ " $ONLY " == *" $1 "* ]]; }

if want miller-code; then open /home/you/Projects/trailhead/src; k "CTRL,2" 1; k ",Down" 0.4; k ",Down" 2.5; ./shot.sh miller-code; fi
if want miller-md; then open /home/you/Documents; k "CTRL,2" 1; for i in 1 2 3; do k ",Down" 0.4; done; sleep 2.5; ./shot.sh miller-md; fi
if want grid; then open /home/you/Pictures; ./shot.sh grid; fi
if want preview; then open /home/you/Pictures; for i in 1 2 3 4 5 6; do k ",Right" 0.3; done; k ",space" 2.5; ./shot.sh preview; fi


if want terminal; then open /home/you/Projects/trailhead; k "CTRL,3" 1; k ",F4" 3; ./app.sh type "git status -s"; k ",Return" 1.5; ./shot.sh terminal; fi
if want split; then open /home/you; for i in 1 2 3 4 5; do k ",Right" 0.3; done; k "CTRL SHIFT,Return" 2.5; ./shot.sh split; fi
if want rename; then open /home/you/Pictures; k "CTRL,a" 0.6; k ",F2" 2; ./app.sh type "-"; k ",Tab" 0.4; k "SHIFT,minus" 1.2; ./shot.sh rename; fi
if want settings; then open /home/you/Pictures; k "CTRL,comma" 3; ./shot.sh settings; fi
if want light; then THEME=rose-pine-dawn open /home/you/Pictures; k "CTRL,2" 0.8; for i in 1 2 3 4 5; do k ",Down" 0.3; done; sleep 2.5; ./shot.sh light; fi
if want detailed; then open /home/you/Pictures; k "CTRL,3" 2.5; ./shot.sh detailed; fi
if want dark; then open /home/you/Pictures; k "CTRL,2" 0.8; for i in 1 2 3 4 5; do k ",Down" 0.3; done; sleep 2.5; ./shot.sh dark; fi
