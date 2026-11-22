#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Records the USB traffic (usbmon) and the uvcvideo trace around a failing
# VIDIOC_STREAMON, then power-cycles the camera's port from software and tries again.
# Every change is reverted at the end.
# Usage: sudo sh tools/usb-diagnose.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
PROBE="$HERE/lensscan-probe.bin"
OUT=/tmp/lensscan-usb-diagnose.txt
MON=/tmp/lensscan-usbmon.txt
RUN_AS=${SUDO_USER:-root}

find_usb() {
  for d in /sys/bus/usb/devices/*; do
    [ -f "$d/idVendor" ] || continue
    [ "$(cat "$d/idVendor")" = "1e4f" ] && { echo "$d"; return; }
  done
}

USBDEV=$(find_usb)
[ -n "$USBDEV" ] || { echo "CZUR camera not found"; exit 1; }
NAME=$(basename "$USBDEV")            # e.g. 1-4
BUS=${NAME%%-*}                       # 1
PORT=${NAME##*-}                      # 4
PORTDIR=/sys/bus/usb/devices/$BUS-0:1.0/usb$BUS-port$PORT

attempt() {
  label=$1
  dev=$(cat "$(find_usb)/devnum")
  echo "=== $label (devnum $dev, runtime $(cat "$(find_usb)/power/runtime_status"))" | tee -a "$OUT"
  dmesg -C
  echo 0x0fff > /sys/module/uvcvideo/parameters/trace
  echo "--- mark $label" >> "$MON"
  sudo -u "$RUN_AS" timeout 15 "$PROBE" --stream 3 2>&1 | tee -a "$OUT"
  echo 0 > /sys/module/uvcvideo/parameters/trace
  dmesg | grep -iE "uvc|usb $NAME|xhci" | tail -25 >> "$OUT"
}

: > "$OUT"
: > "$MON"
uname -a >> "$OUT"
modprobe usbmon
mount | grep -q debugfs || mount -t debugfs none /sys/kernel/debug
cat "/sys/kernel/debug/usb/usbmon/${BUS}u" >> "$MON" &
MONPID=$!
sleep 0.5

attempt "1-current-state"

ORIG_CONTROL=$(cat "$USBDEV/power/control")
echo on > "$USBDEV/power/control"
attempt "2-autosuspend-off"

if [ -w "$PORTDIR/disable" ]; then
  echo "--- port power-cycle $PORTDIR" | tee -a "$OUT"
  echo 1 > "$PORTDIR/disable"
  sleep 4
  echo 0 > "$PORTDIR/disable"
  i=0
  while [ $i -lt 40 ]; do
    [ -n "$(find_usb)" ] && ls "$(find_usb)"/*/video4linux >/dev/null 2>&1 && break
    sleep 0.25; i=$((i+1))
  done
  sleep 1
  echo on > "$(find_usb)/power/control" 2>/dev/null
  attempt "3-after-port-power-cycle"
  sleep 3
  attempt "4-second-session"
else
  echo "port disable file not writable: $PORTDIR" | tee -a "$OUT"
fi

kill $MONPID 2>/dev/null
NEW=$(find_usb)
[ -n "$NEW" ] && echo "$ORIG_CONTROL" > "$NEW/power/control" 2>/dev/null
journalctl -k -b --no-pager -q | grep -E "usb $NAME|uvcvideo $NAME" | tail -20 >> "$OUT"
chmod 644 "$OUT" "$MON"
echo "Report: $OUT  usbmon: $MON"
