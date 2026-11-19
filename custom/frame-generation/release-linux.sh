#!/usr/bin/env bash
# Builds Release and assembles dist/openmw-fg-<version>-linux-x86_64.zip.
# Run on Linux (or from Windows: wsl ./release-linux.sh 0.2.0).
set -euo pipefail

VER="${1:?usage: release-linux.sh <version>}"
ROOT="$(cd "$(dirname "$0")" && pwd)"
# Out-of-tree build dir (repo may live on a slow Windows drvfs mount).
BUILD="${FG_LINUX_BUILD_DIR:-$HOME/.cache/openmw-fg-build}"

GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release "${GEN[@]}"
cmake --build "$BUILD" -j"$(nproc)"

mkdir -p "$ROOT/dist"
STAGE="$(mktemp -d)"
cp "$BUILD/libopenmw_fg.so" "$STAGE/"
strip --strip-unneeded "$STAGE/libopenmw_fg.so"
cp "$ROOT/packaging/openmw_fg_linux.ini" "$STAGE/openmw_fg.ini"
cp "$ROOT/packaging/openmw-fg.sh" "$STAGE/"
chmod +x "$STAGE/openmw-fg.sh"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
sed "s/{VERSION}/$VER/g" "$ROOT/packaging/README-linux.txt" > "$STAGE/README.txt"

ZIP="$ROOT/dist/openmw-fg-$VER-linux-x86_64.zip"
rm -f "$ZIP"
(cd "$STAGE" && zip -q -X "$ZIP" libopenmw_fg.so openmw_fg.ini openmw-fg.sh LICENSE.txt README.txt)
rm -rf "$STAGE"

# source zip: tracked files at HEAD, unzips into openmw-fg-<version>/
# (same name/content as the one release.ps1 makes on Windows)
SRCZIP="$ROOT/dist/openmw-fg-$VER-src.zip"
git -C "$ROOT" archive --format=zip --prefix "openmw-fg-$VER/" -o "$SRCZIP" HEAD

echo "release: $ZIP"
unzip -l "$ZIP"
echo "source:  $SRCZIP"
