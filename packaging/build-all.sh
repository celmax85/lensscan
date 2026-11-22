#!/bin/sh
# Builds every package in Docker. Usage: packaging/build-all.sh [target...]
# Targets: ubuntu-22.04 ubuntu-24.04 ubuntu-26.04 debian-12 debian-13 fedora-43 opensuse-tumbleweed appimage
set -eu
cd "$(dirname "$0")/.."
TARGETS=${*:-"ubuntu-22.04 ubuntu-24.04 ubuntu-26.04 debian-12 debian-13 fedora-43 opensuse-tumbleweed appimage"}
# Builds a container image; prints the end of the log if it fails.
image() { log=$(mktemp); docker build --progress=plain "$@" >"$log" 2>&1 || { tail -40 "$log"; rm -f "$log"; exit 1; }; rm -f "$log"; }
run() { docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/src -w /src "$@"; }
for t in $TARGETS; do
  echo "=== $t"
  case $t in
    ubuntu-*|debian-*)
      base=$(echo "$t" | sed 's/-/:/'); img=lensscan-dev:$t
      image -t "$img" --build-arg BASE="$base" -f docker/Dockerfile.deb docker
      run "$img" packaging/build-deb.sh "dist/$t" ;;
    fedora-*)
      img=lensscan-dev:$t
      image -t "$img" --build-arg BASE="fedora:${t#fedora-}" -f docker/Dockerfile.rpm docker
      run "$img" packaging/build-rpm.sh "dist/$t" ;;
    opensuse-*)
      img=lensscan-dev:$t
      image -t "$img" --build-arg BASE="opensuse/${t#opensuse-}" -f docker/Dockerfile.rpm docker
      run "$img" packaging/build-rpm.sh "dist/$t" ;;
    appimage)
      image -t lensscan-dev:appimage -f docker/Dockerfile.appimage docker
      run lensscan-dev:appimage packaging/appimage/build-appimage.sh dist/appimage ;;
    *) echo "unknown target $t"; exit 1 ;;
  esac
done
