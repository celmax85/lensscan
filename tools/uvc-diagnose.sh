#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Tries known workarounds for VIDIOC_STREAMON failures on the CZUR camera and
# records which one makes streaming work. Every change is reverted at the end.
# Usage: sudo sh tools/uvc-diagnose.sh
set -u
OUT=/tmp/lensscan-uvc-diagnose.txt
USBDEV=""
for d in /sys/bus/usb/devices/*; do
  [ -f "$d/idVendor" ] || continue
  if [ "$(cat "$d/idVendor")" = "1e4f" ]; then USBDEV=$d; break; fi
done
[ -n "$USBDEV" ] || { echo "CZUR camera not found"; exit 1; }
BUS=$(basename "$USBDEV")

find_node() {
  for v in /sys/class/video4linux/video*; do
    if grep -q E012A "$v/name" 2>/dev/null; then echo "/dev/$(basename "$v")"; return; fi
  done
}

try_stream() {
  label=$1
  sleep 2
  node=$(find_node)
  dmesg -C 2>/dev/null || true
  echo 0x0fff > /sys/module/uvcvideo/parameters/trace
  if timeout 15 ffmpeg -loglevel error -f v4l2 -input_format mjpeg -video_size 640x480 \
       -i "$node" -frames:v 1 -y /tmp/lensscan-test.jpg >/tmp/lensscan-ff.txt 2>&1; then
    result=OK
  else
    result="FAIL ($(tail -1 /tmp/lensscan-ff.txt))"
  fi
  echo 0 > /sys/module/uvcvideo/parameters/trace
  {
    echo "=== $label: $result (node $node)"
    dmesg | grep -iE "uvc|usb $BUS|xhci" | tail -15
  } >> "$OUT"
  echo "$label: $result"
}

: > "$OUT"
uname -a >> "$OUT"
echo "device $USBDEV power/control=$(cat "$USBDEV/power/control")" >> "$OUT"

try_stream "0-baseline"

ORIG_CONTROL=$(cat "$USBDEV/power/control")
echo on > "$USBDEV/power/control"
try_stream "1-autosuspend-off"

AUDIO_IFS=""
for i in "$USBDEV"/"$BUS":1.*; do
  drv=$(basename "$(readlink "$i/driver" 2>/dev/null)" 2>/dev/null)
  if [ "$drv" = "snd-usb-audio" ]; then
    AUDIO_IFS="$AUDIO_IFS $(basename "$i")"
    echo "$(basename "$i")" > /sys/bus/usb/drivers/snd-usb-audio/unbind
  fi
done
echo "audio interfaces unbound:$AUDIO_IFS" >> "$OUT"
try_stream "2-audio-unbound"

echo 0 > "$USBDEV/authorized"; sleep 1; echo 1 > "$USBDEV/authorized"
sleep 3
echo on > "$USBDEV/power/control" 2>/dev/null
try_stream "3-after-usb-reauthorize"

# Restore.
for i in $AUDIO_IFS; do
  echo "$i" > /sys/bus/usb/drivers/snd-usb-audio/bind 2>/dev/null
done
echo "$ORIG_CONTROL" > "$USBDEV/power/control" 2>/dev/null
lsusb -t >> "$OUT" 2>&1
chmod 644 "$OUT"
echo "Report saved to $OUT"
