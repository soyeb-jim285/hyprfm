#!/usr/bin/env bash
# scripts/release.sh — cut a new hyprfm release in one step.
#
# Usage:
#   scripts/release.sh 0.4.22 ["One-line release note for metainfo.xml"]
#
# What it does, in order:
#   1. Bumps every in-tree version reference:
#        - CMakeLists.txt   project(hyprfm VERSION X.Y.Z ...)
#        - io.github.soyeb_jim285.HyprFM.yml     hyprfm source `tag: vX.Y.Z`
#        - dist/*.metainfo.xml   screenshot URLs + new <release> entry
#   2. Runs a quick cmake build as a smoke test (if build/ exists).
#   3. Creates a "chore: release vX.Y.Z" commit on the current branch.
#   4. Creates an annotated git tag vX.Y.Z on that commit.
#   5. Stops BEFORE pushing — prints the exact push commands so you can
#      review `git log -1` / `git show HEAD` first and then push by hand.
#
# Rerunning is idempotent as long as the tag doesn't already exist.
set -euo pipefail

if [ $# -lt 1 ] || [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    sed -n '2,/^set -euo/p' "$0" | sed '$d'
    exit 0
fi

VERSION="$1"
NOTE="${2:-}"

if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "release.sh: version must be X.Y.Z (got '$VERSION')" >&2
    exit 1
fi

TAG="v$VERSION"

REPO_ROOT=$(git rev-parse --show-toplevel)
cd "$REPO_ROOT"

CMAKE_FILE="CMakeLists.txt"
FLATPAK_FILE="io.github.soyeb_jim285.HyprFM.yml"
META_FILE="dist/io.github.soyeb_jim285.HyprFM.metainfo.xml"

for f in "$CMAKE_FILE" "$FLATPAK_FILE" "$META_FILE"; do
    [ -f "$f" ] || { echo "release.sh: missing $f" >&2; exit 1; }
done

if [ -n "$(git status --porcelain --untracked-files=no --ignore-submodules=all)" ]; then
    echo "release.sh: working tree has uncommitted tracked changes. Commit or stash first." >&2
    git status --short --ignore-submodules=all
    exit 1
fi

if git rev-parse --verify --quiet "refs/tags/$TAG" >/dev/null; then
    echo "release.sh: tag $TAG already exists. Pick a new version or delete it first." >&2
    exit 1
fi

echo "==> Cutting release $VERSION"

# 1. CMakeLists.txt ---------------------------------------------------------
python3 - "$CMAKE_FILE" "$VERSION" <<'PY'
import re, sys, pathlib
path, version = pathlib.Path(sys.argv[1]), sys.argv[2]
text = path.read_text()
new, n = re.subn(
    r'(project\(hyprfm\s+VERSION\s+)\d+\.\d+\.\d+',
    rf'\g<1>{version}',
    text,
)
if n != 1:
    raise SystemExit(f"release.sh: expected 1 project(hyprfm VERSION ...) in {path}, found {n}")
path.write_text(new)
PY

# 2. Flatpak manifest -------------------------------------------------------
#    Only the hyprfm git source gets bumped (not qtwayland / wl-clipboard /
#    bundled fd etc.). We locate it by the immediately-preceding URL line.
python3 - "$FLATPAK_FILE" "$TAG" <<'PY'
import re, sys, pathlib
path, tag = pathlib.Path(sys.argv[1]), sys.argv[2]
text = path.read_text()
new, n = re.subn(
    r'(url:\s*https://github\.com/soyeb-jim285/hyprfm\.git\s*\n(?:[^\n]*\n)*?\s*tag:\s*)v\d+\.\d+\.\d+',
    rf'\g<1>{tag}',
    text,
)
if n != 1:
    raise SystemExit(f"release.sh: expected 1 hyprfm tag: line in {path}, found {n}")
path.write_text(new)
PY

# 3. metainfo.xml -----------------------------------------------------------
#    - rewrite screenshot URLs: hyprfm/v<old>/docs/screenshots → hyprfm/v<new>/…
#    - prepend a new <release version="X.Y.Z" date="YYYY-MM-DD"> entry if
#      the version isn't already listed.
python3 - "$META_FILE" "$VERSION" "$TAG" "$NOTE" <<'PY'
import re, sys, datetime, pathlib
path, version, tag, note = (pathlib.Path(sys.argv[1]),) + tuple(sys.argv[2:])
text = path.read_text()

# Screenshot URLs
text = re.sub(
    r'(raw\.githubusercontent\.com/soyeb-jim285/hyprfm/)v\d+\.\d+\.\d+',
    rf'\g<1>{tag}',
    text,
)

# Release entry
if f'<release version="{version}"' not in text:
    today = datetime.date.today().isoformat()
    note = note or f'Release {version}.'
    # Escape &<> for XML
    note = (note.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;'))
    entry = (
        f'    <release version="{version}" date="{today}">\n'
        f'      <description>\n'
        f'        <p>{note}</p>\n'
        f'      </description>\n'
        f'    </release>\n'
    )
    new, n = re.subn(r'(<releases>\s*\n)', r'\1' + entry, text, count=1)
    if n != 1:
        raise SystemExit(f"release.sh: <releases> not found in {path}")
    text = new

path.write_text(text)
PY

# 4. Smoke build (only if a build tree already exists) ---------------------
if [ -d build ]; then
    echo "==> Smoke build (reusing existing build/)"
    cmake --build build --target hyprfm >/dev/null
fi

# 5. Commit + tag ----------------------------------------------------------
git add "$CMAKE_FILE" "$FLATPAK_FILE" "$META_FILE"
git commit -m "chore: release $TAG"
git tag -a "$TAG" -m "Release $VERSION"

echo
echo "==> Release $TAG prepared on $(git rev-parse --abbrev-ref HEAD) as $(git rev-parse --short HEAD)"
echo
echo "Push with:"
echo "    git push origin $(git rev-parse --abbrev-ref HEAD) && git push origin $TAG"
echo
echo "To undo locally before pushing:"
echo "    git tag -d $TAG && git reset --hard HEAD^"
