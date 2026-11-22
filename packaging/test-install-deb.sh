#!/bin/sh
# Installs a .deb in a clean container and starts the app headless.
# Usage: packaging/test-install-deb.sh IMAGE DEB
set -eu
IMG=$1; DEB=$2
docker run --rm -v "$(cd "$(dirname "$DEB")" && pwd)":/pkg "$IMG" sh -c "
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq >/dev/null && apt-get install -y -qq /pkg/$(basename "$DEB") >/dev/null 2>&1 || { apt-get install -y /pkg/$(basename "$DEB"); exit 1; }
  lensscan --version
  mkdir -p /tmp/snap && LENSSCAN_SNAPSHOT=/tmp/snap:2 QT_QPA_PLATFORM=offscreen lensscan 2>&1 | grep -v portal; ls /tmp/snap
  lensscan-probe 2>&1 | head -3"
