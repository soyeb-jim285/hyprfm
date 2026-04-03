#!/bin/bash
# Syncs PKGBUILD + .SRCINFO to the AUR repo after pushing hyprfm
set -e

MAIN_REPO="$HOME/hyprfm"
AUR_REPO="$HOME/hyprfm-aur"

cd "$MAIN_REPO"

# Compute pkgver the same way PKGBUILD does
PKGVER="r$(git rev-list --count HEAD).g$(git rev-parse --short HEAD)"

cd "$AUR_REPO"

# Update pkgver in PKGBUILD
sed -i "s/^pkgver=.*/pkgver=$PKGVER/" PKGBUILD

# Copy over dependency/build changes from main repo PKGBUILD
# (only the pkgver line differs day-to-day, but full sync catches dep changes)
cp "$MAIN_REPO/PKGBUILD" PKGBUILD
sed -i "s/^pkgver=.*/pkgver=$PKGVER/" PKGBUILD

# Regenerate .SRCINFO
makepkg --printsrcinfo > .SRCINFO

# Commit and push if there are changes
if git diff --quiet && git diff --cached --quiet; then
    echo "AUR already up to date ($PKGVER)"
    exit 0
fi

git add PKGBUILD .SRCINFO
git commit -m "update pkgver to $PKGVER"
git push
echo "AUR updated to $PKGVER"
