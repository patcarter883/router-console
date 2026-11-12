#!/usr/bin/env bash
# Capture the config-backup window after a real backup run and a staged restore,
# driven through the window itself (xdotool clicks), on the headless display.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=${OUT:-docs/evidence/m12-config-backup.png}
HOST=${HOST:-127.0.0.1:20443}
DISPLAY_=${DISPLAY_:-:99}
BACKUP_DIR=${BACKUP_DIR:-/tmp/m12-shots}

mkdir -p "$BACKUP_DIR"
if ! xdpyinfo -display "$DISPLAY_" >/dev/null 2>&1; then
    Xvfb "$DISPLAY_" -screen 0 1400x900x24 >/tmp/xvfb-m12.log 2>&1 &
    sleep 1
fi

export DISPLAY=$DISPLAY_
export QT_QUICK_BACKEND=software

./build/src/router-console --connect "$HOST" --user root --password root --accept-new-cert \
    --open-tool backup >/tmp/router-console-m12-backup.log 2>&1 &
APP=$!
trap 'kill $APP 2>/dev/null || true' EXIT
sleep 7

WIN=$(xdotool search --name "Config backup" | head -1)
echo "window: $WIN"
xdotool getwindowgeometry "$WIN"
xdotool windowfocus --sync "$WIN" || true

# Target directory field, then "Backup now".
xdotool mousemove --window "$WIN" 300 25 click 1
sleep 0.5
xdotool key ctrl+a
xdotool type --delay 25 "$BACKUP_DIR"
sleep 0.5
xdotool mousemove --window "$WIN" 855 25 click 1
sleep 6

ARCHIVE=$(ls -t "$BACKUP_DIR"/router-console-backup-*.tar.gz 2>/dev/null | head -1 || true)
echo "archive: ${ARCHIVE:-none}"
if [ -n "$ARCHIVE" ]; then
    # Restore field, then "Stage restore" (never the confirm button).
    xdotool mousemove --window "$WIN" 300 62 click 1
    sleep 0.5
    xdotool key ctrl+a
    xdotool type --delay 15 "$ARCHIVE"
    sleep 0.5
    xdotool mousemove --window "$WIN" 855 62 click 1
    sleep 6
fi

import -display "$DISPLAY_" -window root "$OUT"
echo "screenshot: $OUT"
ls -l "$OUT"
