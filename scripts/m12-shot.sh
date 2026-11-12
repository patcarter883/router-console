#!/usr/bin/env bash
# Capture one milestone-12 tool window on a headless X server (default :99; the
# shared :98 belongs to whoever is capturing there). The window opens via
# --open-tool, the app is killed afterwards, nothing else on the host is touched.
#
#   TOOL=log OUT=docs/evidence/m12-log-viewer.png scripts/m12-shot.sh
set -euo pipefail
cd "$(dirname "$0")/.."

TOOL=${TOOL:-log}
OUT=${OUT:-/tmp/m12-$TOOL.png}
HOST=${HOST:-127.0.0.1:20443}
DISPLAY_=${DISPLAY_:-:99}
WAIT=${WAIT:-8}
SIZE=${SIZE:-1024x768}

if ! xdpyinfo -display "$DISPLAY_" >/dev/null 2>&1; then
    Xvfb "$DISPLAY_" -screen 0 "${SIZE}x24" >/tmp/xvfb-m12.log 2>&1 &
    XVFB=$!
    trap 'kill $XVFB 2>/dev/null || true' EXIT
    sleep 1
fi

export DISPLAY=$DISPLAY_
export QT_QUICK_BACKEND=software

./build/src/router-console --connect "$HOST" --user root --password root --accept-new-cert \
    --open-tool "$TOOL" >"/tmp/router-console-m12-$TOOL.log" 2>&1 &
APP=$!
trap 'kill $APP 2>/dev/null || true' EXIT
sleep "$WAIT"

import -display "$DISPLAY_" -window root "$OUT"
echo "screenshot: $OUT"
ls -l "$OUT"

if grep -qiE "qrc:/.*error|TypeError|is not defined|Cannot assign|ReferenceError" \
    "/tmp/router-console-m12-$TOOL.log"; then
    echo "--- QML problems:"
    grep -iE "qrc:/.*error|TypeError|is not defined|Cannot assign|ReferenceError" \
        "/tmp/router-console-m12-$TOOL.log" | head -10
    exit 1
fi
