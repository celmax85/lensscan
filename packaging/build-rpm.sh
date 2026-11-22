#!/bin/sh
# Builds the .rpm inside a Fedora/openSUSE container. Run from the source root:
#   packaging/build-rpm.sh OUTDIR
set -eu
OUT=${1:-dist}
TOP=$(mktemp -d)
VER=$(sed -n 's/^Version: *//p' packaging/rpm/lensscan.spec)
mkdir -p "$TOP/SOURCES" "$TOP/stage/lensscan-$VER"
tar --exclude=./build --exclude=./dist --exclude=./.git -cf - . | tar -xf - -C "$TOP/stage/lensscan-$VER"
tar -czf "$TOP/SOURCES/lensscan-$VER.tar.gz" -C "$TOP/stage" lensscan-$VER
rpmbuild --define "_topdir $TOP" -bb packaging/rpm/lensscan.spec
mkdir -p "$OUT"
find "$TOP/RPMS" -name "lensscan-$VER-*.rpm" -exec cp {} "$OUT"/ \;
rm -rf "$TOP"
ls -la "$OUT"
