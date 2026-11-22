#!/bin/sh
# Builds every package in Docker. Usage: packaging/build-all.sh [target...]
# Targets: ubuntu-22.04 ubuntu-24.04 ubuntu-26.04 debian-12 debian-13 fedora-43 opensuse-tumbleweed appimage
set -eu
cd "$(dirname "$0")/.."
TARGETS=${*:-"ubuntu-22.04 ubuntu-24.04 ubuntu-26.04 debian-12 debian-13 fedora-43 opensuse-tumbleweed appimage"}
run() { docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/src -w /src "$@"; }
for t in $TARGETS; do
  echo "=== $t"
  case $t in
    ubuntu-*|debian-*)
      base=$(echo "$t" | sed 's/-/:/'); img=lensscan-dev:$t
      docker build -q -t "$img" --build-arg BASE="$base" -f docker/Dockerfile.deb docker >/dev/null
      run "$img" packaging/build-deb.sh "dist/$t" ;;
    fedora-*)
      img=lensscan-dev:$t
      docker build -q -t "$img" --build-arg BASE="fedora:${t#fedora-}" -f docker/Dockerfile.rpm docker >/dev/null
      run "$img" packaging/build-rpm.sh "dist/$t" ;;
    opensuse-*)
      img=lensscan-dev:$t
      docker build -q -t "$img" --build-arg BASE="opensuse/${t#opensuse-}" -f docker/Dockerfile.rpm docker >/dev/null
      run "$img" packaging/build-rpm.sh "dist/$t" ;;
    appimage)
      docker build -q -t lensscan-dev:appimage -f docker/Dockerfile.appimage docker >/dev/null
      run lensscan-dev:appimage packaging/appimage/build-appimage.sh dist/appimage ;;
    *) echo "unknown target $t"; exit 1 ;;
  esac
done
