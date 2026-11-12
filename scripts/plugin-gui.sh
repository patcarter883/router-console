#!/usr/bin/env bash
# Launch the client on a headless X server, connect to the dev VM, open one
# plugin window and capture the screen. Milestone-6 evidence for the native
# custom-app views.
#
#   PLUGIN=modemaim OUT=/tmp/plugin.png scripts/plugin-gui.sh
set -euo pipefail
cd "$(dirname "$0")/.."

PLUGIN=${PLUGIN:-rist2rist}
OUT=${OUT:-/tmp/router-console-plugin-$PLUGIN.png}
HOST=${HOST:-127.0.0.1:10443}
USER_=${USER_:-root}
PASS=${PASS:-root}
DISPLAY_=${DISPLAY_:-:97}

# PLUGIN=tree captures the left tree only (no plugin window opened).
PLUGIN_ARGS=()
if [ "$PLUGIN" != "tree" ]; then
    PLUGIN_ARGS=(--open-plugin "$PLUGIN")
fi

Xvfb "$DISPLAY_" -screen 0 1400x900x24 >/tmp/xvfb-plugin.log 2>&1 &
XVFB=$!
APP=""
trap 'kill $APP $XVFB 2>/dev/null || true' EXIT
sleep 1

export DISPLAY=$DISPLAY_
export QT_QUICK_BACKEND=software
./build/src/router-console --connect "$HOST" --user "$USER_" --password "$PASS" \
    --accept-new-cert "${PLUGIN_ARGS[@]}" >"/tmp/router-console-plugin-$PLUGIN.log" 2>&1 &
APP=$!
sleep "${WAIT:-9}"

import -display "$DISPLAY_" -window root "$OUT"
echo "screenshot: $OUT"

if grep -qiE "qrc:/.*error|TypeError|is not defined|Cannot assign" "/tmp/router-console-plugin-$PLUGIN.log"; then
    echo "--- QML problems:"
    grep -iE "qrc:/.*error|TypeError|is not defined|Cannot assign" "/tmp/router-console-plugin-$PLUGIN.log"
    exit 1
fi
echo "no QML errors in the log"
