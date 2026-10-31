#!/usr/bin/env bash
# shot.sh <name> : grab CAP to ../media/raw/<name>.png and a preview
cd "$(dirname "$0")"; mkdir -p ../media/raw run/prev
./app.sh grab ../media/raw/$1.png && magick ../media/raw/$1.png -resize 1100x run/prev/$1.png
