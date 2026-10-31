#!/usr/bin/env bash
cd "$(dirname "$0")"
want(){ [ -z "$ONLY" ] || [[ " $ONLY " == *" $1 "* ]]; }
want preview  && ./record.sh preview /home/you/Pictures 'k ",Right" 0.3' 'k ",Right" 0.3' 'k ",Right" 0.3' 'k ",Right" 0.3' 'k ",Right" 0.5' 'k ",space" 1.6' 'k ",Right" 1.2' 'k ",Right" 1.2' 'k ",Right" 1.2' 'k ",Escape" 0.8'
want terminal && ./record.sh terminal /home/you/Projects/trailhead 'k "CTRL,3" 0.8' 'k ",F4" 1.5' 't "git status -s"' 'w 0.3' 'k ",Return" 1.6'
want split    && ./record.sh split /home/you 'for i in 1 2 3 4 5; do k ",Right" 0.25; done' 'w 0.4' 'k "CTRL SHIFT,Return" 1.6' 'k ",F3" 1.2' 'k ",F3" 1.4'
want docs     && ./record.sh docs /home/you/Documents 'k "CTRL,2" 1' 'k ",Down" 1.2' 'k ",Down" 1.6' 'k ",Down" 1.8' 'k ",Up" 0.5' 'k ",Up" 0.5' 'k ",Right" 1.2' 'k ",Down" 1.0' 'k ",Down" 1.0' 'k ",Down" 1.4'
want rename   && ./record.sh rename /home/you/Pictures 'k "CTRL,a" 0.8' 'k ",F2" 1.4' 't "-"' 'w 0.8' 'k ",Tab" 0.3' 'k "SHIFT,minus" 1.8'
want light    && THEME=rose-pine-dawn ./record.sh light /home/you/Pictures 'k "CTRL,2" 1' 'for i in 1 2 3 4 5 6 7 8 9 10; do k ",Down" 0.7; done'
