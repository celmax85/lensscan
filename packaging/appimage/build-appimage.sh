#!/bin/sh
# Builds LensScan-x86_64.AppImage in the docker/Dockerfile.appimage container.
#   packaging/appimage/build-appimage.sh OUTDIR
set -eu
OUT=$(mkdir -p "${1:-dist/appimage}" && cd "${1:-dist/appimage}" && pwd)
SRC=$(pwd)
WORK=$(mktemp -d)
cmake -S "$SRC" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DLENSSCAN_BUILD_TESTS=OFF
cmake --build "$WORK/build"
DESTDIR="$WORK/AppDir" cmake --install "$WORK/build"
# The udev rule cannot be installed from an AppImage; ship it for the user to copy.
mkdir -p "$WORK/AppDir/usr/share/lensscan"
mv "$WORK/AppDir/usr/lib/udev/rules.d/70-lensscan.rules" "$WORK/AppDir/usr/share/lensscan/"
# OCR: English, French and orientation detection, plus the font for searchable PDFs.
TESS=$(ls -d /usr/share/tesseract-ocr/*/tessdata | head -1)
mkdir -p "$WORK/AppDir/usr/share/tessdata"
cp "$TESS"/eng.traineddata "$TESS"/fra.traineddata "$TESS"/osd.traineddata "$TESS"/pdf.ttf \
   "$WORK/AppDir/usr/share/tessdata/"
cd "$WORK"
export QMAKE=/usr/lib/qt6/bin/qmake
export EXTRA_QT_PLUGINS="waylandcompositor;wayland-shell-integration;wayland-decoration-client;wayland-graphics-integration-client"
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so;libqoffscreen.so"
export LDAI_OUTPUT="$OUT/LensScan-x86_64.AppImage"
linuxdeploy --appdir AppDir \
  --desktop-file AppDir/usr/share/applications/io.github.celmax85.LensScan.desktop \
  --icon-file AppDir/usr/share/icons/hicolor/scalable/apps/io.github.celmax85.LensScan.svg \
  --plugin qt --output appimage
rm -rf "$WORK"
ls -la "$OUT"
