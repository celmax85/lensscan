#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Collects uvcvideo kernel traces while trying to start the CZUR stream.
# Usage: sudo sh tools/uvc-trace.sh [/dev/videoN]
set -u
DEV=${1:-/dev/video2}
OUT=/tmp/lensscan-uvc-trace.txt
echo 0x0fff > /sys/module/uvcvideo/parameters/trace
dmesg -C 2>/dev/null || true
ffmpeg -loglevel error -f v4l2 -input_format mjpeg -video_size 640x480 -i "$DEV" -frames:v 1 -y /tmp/lensscan-test.jpg
echo 0 > /sys/module/uvcvideo/parameters/trace
{
  uname -a
  dmesg | grep -iE "uvc|usb 1-|xhci" | tail -80
} > "$OUT"
chmod 644 "$OUT"
echo "Trace saved to $OUT"
