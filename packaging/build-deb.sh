#!/bin/sh
# Builds the .deb inside a Debian/Ubuntu container. Run from the source root:
#   packaging/build-deb.sh OUTDIR
set -eu
OUT=${1:-dist}
WORK=$(mktemp -d)
tar --exclude=./build --exclude=./dist --exclude=./.git -cf - . | tar -xf - -C "$WORK"
cp -r packaging/debian "$WORK/debian"
(cd "$WORK" && dpkg-buildpackage -us -uc -b -j"$(nproc)")
mkdir -p "$OUT"
cp "$WORK"/../lensscan_*.deb "$OUT"/
rm -rf "$WORK" "$WORK"/../lensscan_*
ls -la "$OUT"
