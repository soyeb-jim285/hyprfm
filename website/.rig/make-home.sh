#!/usr/bin/env bash
# Builds a believable, private-data-free home for capture at ./home (mounted as /home/you).
set -e
cd "$(dirname "$0")"
R=$PWD; H=$R/home
rm -rf "$H"; mkdir -p "$H"/{Desktop,Documents/notes,Downloads,Music/"Field recordings",Pictures/Wallpapers,Pictures/Trips,Projects,Videos,.config/hyprfm}
cd "$H"
# Pictures
for k in estuary cirque dunes herbert esztergom howth milkyway beskydy; do cp "$R/photos/$k.jpg" Pictures/; done
mv Pictures/estuary.jpg Pictures/conwy-estuary-dusk.jpg; mv Pictures/cirque.jpg Pictures/mylgval-cirque.jpg
mv Pictures/dunes.jpg Pictures/namib-dunes.jpg; mv Pictures/herbert.jpg Pictures/herbert-lake-mist.jpg
mv Pictures/esztergom.jpg Pictures/esztergom-blue-hour.jpg; mv Pictures/howth.jpg Pictures/howth-head-gorse.jpg
mv Pictures/milkyway.jpg Pictures/oeschinensee-perseids.jpg; mv Pictures/beskydy.jpg Pictures/beskydy-october.jpg
magick "$R/../media/fog-orig.jpg" -resize 2560x Pictures/Wallpapers/savoureuse-fog.jpg
cp Pictures/namib-dunes.jpg Pictures/Wallpapers/dunes-wide.jpg
cp Pictures/oeschinensee-perseids.jpg Pictures/Trips/
cp Pictures/herbert-lake-mist.jpg Pictures/Trips/
# Videos: a slow pan over a photo, so thumbnails and preview have a real poster
ffmpeg -loglevel error -y -loop 1 -i Pictures/conwy-estuary-dusk.jpg -vf "scale=1920:-2,zoompan=z='1+0.0015*on':d=150:s=1280x720:fps=30" -t 5 -c:v libx264 -pix_fmt yuv420p Videos/conwy-timelapse.mp4
ffmpeg -loglevel error -y -loop 1 -i Pictures/namib-dunes.jpg -vf "scale=1920:-2,zoompan=z='1.2-0.001*on':d=150:s=1280x720:fps=30" -t 5 -c:v libx264 -pix_fmt yuv420p Videos/namib-drone.mp4
# Music: short tones as real audio files
for n in "dawn chorus" "harbour wind" "rain on tin"; do ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=$((200 + RANDOM % 400)):duration=4" -c:a libvorbis "Music/Field recordings/$n.ogg"; done
# Documents
cat > Documents/README.md <<'MD'
# Field notes

A short list of places worth walking back to.

## October

- **Beskydy**, the knights' trail from Nýdek, beeches turning red
- **Savoureuse valley**, fog at sunset, stay until the lights come on
- **Conwy**, low tide at dusk, boats sitting on the sand

> Pack a head torch. The descent is longer than it looks.

| Place | Distance | Climb |
|---|---|---|
| Velká Čantoryje | 11 km | 640 m |
| Ballon d'Alsace | 8 km | 420 m |
MD
cat > Documents/notes/packing.toml <<'T'
[pack]
layers = ["merino base", "fleece", "shell"]
water_l = 2.5
head_torch = true

[route]
start = "Nýdek"
summit = "Velká Čantoryje"
T
cat > Documents/notes/route.py <<'PY'
from dataclasses import dataclass


@dataclass
class Leg:
    name: str
    km: float
    climb_m: int


def naismith(leg: Leg) -> float:
    """Hours for a leg: 5 km/h plus 1 h per 600 m of climb."""
    return leg.km / 5 + leg.climb_m / 600


legs = [Leg("Nýdek to Filipka", 4.2, 380), Leg("Filipka to summit", 6.8, 260)]
print(f"{sum(naismith(l) for l in legs):.1f} h")
PY
magick -size 1240x1754 xc:'#f4f1ea' -fill '#20242e' -font /usr/share/fonts/noto/NotoSans-Bold.ttf -pointsize 54 -annotate +110+220 'Trail map, Beskydy' -pointsize 26 -fill '#4a5060' -annotate +110+290 'October edition' \( Pictures/beskydy-october.jpg -resize 1020x \) -geometry +110+360 -composite Documents/beskydy-trail-map.pdf
echo "2026-10-12  Ballon d'Alsace, fog below 900 m" > Documents/notes/log.txt
# Downloads
tar -C Pictures -czf Downloads/trip-photos.tar.gz Trips
(cd Documents && zip -qr ../Downloads/notes-backup.zip notes)
cp Documents/beskydy-trail-map.pdf Downloads/
# Projects: a real git repo with staged, modified and untracked files
mkdir -p Projects/trailhead/{src,docs}
cd Projects/trailhead
git init -q -b main; git config user.name you; git config user.email you@example.com
printf '# trailhead\n\nPlans day walks from a GPX file.\n' > README.md
cp "$H/Documents/notes/route.py" src/route.py
printf 'gpxpy==1.6.2\n' > requirements.txt
printf 'build/\n__pycache__/\n' > .gitignore
git add -A; git commit -qm "first walk planner"
printf '\ndef total(legs):\n    return sum(naismith(l) for l in legs)\n' >> src/route.py
printf 'Elevation comes from SRTM tiles.\n' > docs/elevation.md; git add docs/elevation.md
printf 'import route\n' > src/cli.py
cd "$H"
mkdir -p Projects/dotfiles/hypr; printf 'general {\n    gaps_out = 18\n}\n' > Projects/dotfiles/hypr/hyprland.conf
# timestamps spread over recent weeks so Modified columns read naturally
i=0; find "$H" -mindepth 1 -not -path '*/.git*' | sort | while read f; do i=$((i+1)); touch -d "2026-10-$(( (i % 28) + 1 )) $(( 8 + i % 12 )):$(( i * 7 % 60 )):00" "$f"; done
cat > "$H/.config/user-dirs.dirs" <<'U'
XDG_DESKTOP_DIR="$HOME/Desktop"
XDG_DOCUMENTS_DIR="$HOME/Documents"
XDG_DOWNLOAD_DIR="$HOME/Downloads"
XDG_MUSIC_DIR="$HOME/Music"
XDG_PICTURES_DIR="$HOME/Pictures"
XDG_VIDEOS_DIR="$HOME/Videos"
U
cat > "$H/.config/hyprfm/config.toml" <<C
[general]
theme = "${THEME:-catppuccin-mocha}"
default_view = "grid"
dependency_startup_check = false

[appearance]
anim_duration_fast = $((100 * ${SLOW:-1}))
anim_duration = $((200 * ${SLOW:-1}))
anim_duration_slow = $((350 * ${SLOW:-1}))
transparency_enabled = true
transparency_level = 0.72
radius_small = 6
radius_medium = 10
radius_large = 14

[list_view]
columns = ["name", "git", "size", "modified", "type"]

[miller_view]
parent_fraction = 0.18
current_fraction = 0.34

[bookmarks]
paths = ["~/Documents", "~/Downloads", "~/Pictures", "~/Projects", "~/Videos"]
C
