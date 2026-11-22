#!/bin/sh
# Starts the AppImage headless in clean containers that have the usual desktop libraries
# (OpenGL, fonts, harfbuzz, ...), which every AppImage expects from the host.
set -u
APP=$(cd "$(dirname "${1:-dist/appimage/LensScan-x86_64.AppImage}")" && pwd)
for img in ${IMAGES:-ubuntu:22.04 ubuntu:24.04 debian:12 fedora:43}; do
  printf '== %s: ' "$img"
  docker run --rm -e APPIMAGE_EXTRACT_AND_RUN=1 -v "$APP":/a "$img" sh -c '
    if command -v apt-get >/dev/null; then
      apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq libegl1 libgl1 libopengl0 \
        libfontconfig1 libfreetype6 libharfbuzz0b libglib2.0-0 libxkbcommon0 libdbus-1-3 libx11-6 libx11-xcb1 >/dev/null 2>&1
    else
      dnf install -y -q libglvnd-egl libglvnd-glx libglvnd-opengl fontconfig freetype harfbuzz glib2 libxkbcommon \
        dbus-libs libX11 libX11-xcb >/dev/null 2>&1
    fi
    /a/LensScan-x86_64.AppImage --version || exit 1
    mkdir -p /tmp/s && LENSSCAN_SNAPSHOT=/tmp/s:2 QT_QPA_PLATFORM=offscreen /a/LensScan-x86_64.AppImage >/tmp/log 2>&1
    ls /tmp/s/*.png >/dev/null 2>&1 && echo "    window OK" || { echo "    FAILED"; grep -v -e locale -e UTF /tmp/log; }' 2>&1 | grep -v -e Unable -e Pull -e Digest -e Status -e Download -e Waiting -e Verifying -e "Already exists"
done
