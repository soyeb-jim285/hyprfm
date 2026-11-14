#!/usr/bin/env bash
# Move the built-in terminal's Ghostty pin to another commit, everywhere:
#   CMakeLists.txt                     FetchContent URL + SHA256
#   flake.nix / flake.lock             ghostty input
#   io.github.soyeb_jim285.HyprFM.yml  archive URL + sha256
#   flatpak/ghostty-zig-packages.json  the Zig packages libghostty-vt needs, from Ghostty's list
#
#   scripts/update-ghostty.sh <commit>
set -euo pipefail
commit=${1:?usage: $0 <ghostty commit sha>}
[[ $commit =~ ^[0-9a-f]{40}$ ]] || { echo "need a full 40-char commit sha" >&2; exit 1; }
cd "$(dirname "$0")/.."

old=$(grep -o 'ghostty/archive/[0-9a-f]\{40\}' CMakeLists.txt | cut -d/ -f3)
url="https://github.com/ghostty-org/ghostty/archive/$commit.tar.gz"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
curl -fsSL "$url" -o "$tmp/g.tar.gz"
sha=$(sha256sum "$tmp/g.tar.gz" | cut -d' ' -f1)
oldsha=$(grep -o 'SHA256=[0-9a-f]\{64\}' CMakeLists.txt | cut -d= -f2)
tar -xzf "$tmp/g.tar.gz" -C "$tmp" --strip-components=1 \
    --wildcards '*/flatpak/zig-packages.json' '*/build.zig.zon' '*/build.zig.zon.json'

sed -i "s/$old/$commit/g; s/$oldsha/$sha/g" CMakeLists.txt flake.nix io.github.soyeb_jim285.HyprFM.yml
# Ghostty's list covers the whole app (37 packages, some pinned to commits
# no branch reaches any more); keep only what `zig build -Demit-lib-vt` reads.
# A missing one fails the Flatpak build naming it: add it here.
python3 - "$tmp" > flatpak/ghostty-zig-packages.json <<'PY'
import json, sys
need = {"aro", "translate_c", "uucode", "zlib", "pixels", "highway", "iterm2_themes", "wuffs"}
names = {h: d["name"] for h, d in json.load(open(sys.argv[1] + "/build.zig.zon.json")).items()}
srcs = json.load(open(sys.argv[1] + "/flatpak/zig-packages.json"))
keep = [s for s in srcs if names.get(s["dest"].rsplit("/", 1)[1]) in need]
missing = need - {names[s["dest"].rsplit("/", 1)[1]] for s in keep}
assert not missing, f"not in Ghostty's package list any more: {missing}"
json.dump(keep, sys.stdout, indent=2); print()
PY
if command -v nix >/dev/null; then nix flake update ghostty; fi

echo "ghostty $old -> $commit"
echo "zig needed: $(grep -o 'minimum_zig_version = "[^"]*"' "$tmp/build.zig.zon")"
echo "If that changed, also bump: the zig module in the Flatpak manifest, zig_0_xx in"
echo "flake.nix, setup-zig in .github/workflows/build.yml and the AppImage builder's zig."
