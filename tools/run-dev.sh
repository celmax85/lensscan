#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs a development build without installing it.
#   BUILD_DIR        build directory (default: build/u2604, then build)
#   LENSSCAN_RUNTIME optional root of locally extracted packages (usr/lib/..., usr/share/...)
#                    for libraries that are not installed on the host (Qt Concurrent,
#                    TurboJPEG, Tesseract, Leptonica) and the Tesseract language data.
HERE=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${BUILD_DIR:-}
[ -z "$BUILD" ] && { [ -x "$HERE/build/u2604/lensscan" ] && BUILD="$HERE/build/u2604" || BUILD="$HERE/build"; }
ROOT=${LENSSCAN_RUNTIME:-}
if [ -n "$ROOT" ]; then
  RT="$ROOT/usr/lib/x86_64-linux-gnu"
  [ -d "$RT" ] && export LD_LIBRARY_PATH="$RT${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  [ -z "${TESSDATA_PREFIX:-}" ] && [ -d "$ROOT/usr/share/tesseract-ocr/5/tessdata" ] &&
    export TESSDATA_PREFIX="$ROOT/usr/share/tesseract-ocr/5/tessdata"
fi
exec "$BUILD/lensscan" "$@"
