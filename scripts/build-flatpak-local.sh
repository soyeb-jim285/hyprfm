#!/usr/bin/env bash
# Build the current tracked workspace, including submodules, on a test branch.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"
BUILDER=(flatpak-builder)
if ! command -v flatpak-builder >/dev/null 2>&1; then
    BUILDER=(flatpak run org.flatpak.Builder)
fi
mkdir -p build/packages
SOURCE_DIR="$(mktemp -d "$ROOT_DIR/build/flatpak-source.XXXXXX")"
trap 'rm -rf "$SOURCE_DIR"' EXIT
# Copy tracked working files rather than the old release tag or build artifacts.
git ls-files --recurse-submodules -z | tar --null -T - -cf - | tar -xf - -C "$SOURCE_DIR"
"${BUILDER[@]}" --show-manifest io.github.soyeb_jim285.HyprFM.yml |
    python3 -c '
import json, sys
manifest = json.load(sys.stdin)
module = next(m for m in manifest["modules"] if m["name"] == "hyprfm")
# Swap only the hyprfm git source; keep Ghostty and its Zig packages.
module["sources"] = [{"type": "dir", "path": sys.argv[1]} if s.get("url", "").endswith("soyeb-jim285/hyprfm.git") else s
                  for s in module["sources"]]
assert module["sources"][0]["type"] == "dir"
json.dump(manifest, sys.stdout, indent=2)
' "$SOURCE_DIR" > build/flatpak-local.json
"${BUILDER[@]}" --force-clean --user --jobs="${JOBS:-2}" \
    --repo=build/flatpak-repo --default-branch=testing \
    build/flatpak-local build/flatpak-local.json
flatpak build-bundle build/flatpak-repo \
    "build/packages/HyprFM-testing-$(flatpak --default-arch).flatpak" \
    io.github.soyeb_jim285.HyprFM testing
