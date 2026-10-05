#!/bin/sh
# Usage: build-demo-home.sh TARGET_HOME   (e.g. /home/ricer). Safe to re-run.
set -eu
T=${1:?usage: build-demo-home.sh TARGET_HOME}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$T"; T=$(cd "$T" && pwd)

# 1. text tree (cp -R src/. keeps dotfiles) + empty dirs git cannot hold
rm -rf "$T/Code/rice-o-meter/.git"
cp -R "$HERE/home/." "$T/"
mkdir -p "$T/Pictures/Screenshots" "$T/Downloads" "$T/Fonts"

# 2. media (downloads + magick/ffmpeg)
sh "$HERE/fetch.sh" "$T"

# 3. thesis PDF (page 1 is the cover)
chromium --headless --no-sandbox --disable-gpu --no-pdf-header-footer \
  --print-to-pdf="$T/Documents/thesis-final-FINAL-v3.pdf" "file://$T/Documents/thesis.html"

# 4. Downloads
cp "$HERE/archive-src/definitely-not-a-virus.AppImage" "$T/Downloads/"
chmod +x "$T/Downloads/definitely-not-a-virus.AppImage"
tar -C "$HERE/archive-src" -czf "$T/Downloads/hyprland-0.99-NIGHTLY.tar.gz" hyprland-0.99-NIGHTLY
( cd "$HERE/archive-src" && python3 -m zipfile -c "$T/Downloads/dotfiles-from-reddit.zip" dotfiles-from-reddit )
cp "$T/Pictures/Wallpapers/aurora-from-orbit.jpg" "$T/Downloads/wallpaper(1).jpg"
cp "$T/Pictures/Wallpapers/aurora-from-orbit.jpg" "$T/Downloads/wallpaper(2).jpg"

# 5. fonts (OFL, installed by CI: ttf-jetbrains-mono ttf-fira-code inter-font)
for f in JetBrainsMono-Regular.ttf FiraCode-Regular.ttf Inter-Regular.otf Inter-Regular.ttf; do
  p=$(find /usr/share/fonts -iname "$f" 2>/dev/null | head -n1)
  [ -n "$p" ] && cp "$p" "$T/Fonts/" || echo "font not found: $f" >&2
done

# 6. hidden extras
printf 'hyprctl reload\nhyprctl reload\nhyprctl reload\nnvim hyprland.conf\ngit commit -m "tiny tweak"\nrm -rf node_modules # why\nfastfetch\n' > "$T/.zsh_history"

# 7. git state in Code/rice-o-meter: clean commit, then staged + unstaged + untracked
G="git -c user.name=Demo -c user.email=demo@example.com"
cd "$T/Code/rice-o-meter"
git init -q -b main
git add -A && $G commit -q -m "rice-o-meter: first measurement"
echo 'emoji = true       # I lied' >> config.toml && git add config.toml     # staged
echo '# TODO: detect nix users and offer sympathy' >> rice.py                 # modified
printf 'idea: measure the rice of other people\nidea: leaderboard (me first)\n' > ideas.txt   # untracked

# 8. believable mtimes
touch -d '2026-06-30 03:12' "$T/Documents/thesis.html" "$T/Documents/todo.md"
touch -d '2026-05-09 23:59' "$T/Documents/rice-budget.csv"
echo "demo home ready: $T"
