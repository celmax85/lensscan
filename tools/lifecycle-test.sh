#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Run right after a reboot, with the CZUR plugged in and no other video app open.
# Tests the stream lifecycle the app relies on. No sudo needed.
P="$(dirname "$0")/lensscan-probe.bin"
OUT=/tmp/lensscan-lifecycle.txt
{
  date; uname -r
  echo "== A: first session (single start at full res, then close)"; "$P" --stream 5
  echo "== B: second session (reopen after close)";                 "$P" --stream 5
  sleep 3
  echo "== C: third session after 3 s idle (autosuspend)";          "$P" --stream 5
  echo "== D: restarts inside one session";                          "$P" --restart-test
} 2>&1 | tee "$OUT"
echo "Saved to $OUT"
