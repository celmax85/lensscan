#!/bin/sh
# Builds the .rpm inside a Fedora/openSUSE container. Run from the source root:
#   packaging/build-rpm.sh OUTDIR
set -eu
OUT=${1:-dist}
TOP=$(mktemp -d)
mkdir -p "$TOP/SOURCES" "$TOP/stage/lensscan-0.1.0"
tar --exclude=./build --exclude=./dist --exclude=./.git -cf - . | tar -xf - -C "$TOP/stage/lensscan-0.1.0"
tar -czf "$TOP/SOURCES/lensscan-0.1.0.tar.gz" -C "$TOP/stage" lensscan-0.1.0
rpmbuild --define "_topdir $TOP" -bb packaging/rpm/lensscan.spec
mkdir -p "$OUT"
find "$TOP/RPMS" -name 'lensscan-0*.rpm' -exec cp {} "$OUT"/ \;
rm -rf "$TOP"
ls -la "$OUT"
