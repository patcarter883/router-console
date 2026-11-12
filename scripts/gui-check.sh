#!/usr/bin/env bash
# Launch the client on a headless X server, connect to the dev VM, capture the
# window. Used as the connect-window check when no desktop session is active.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${OUT:-/tmp/router-console-gui.png}
HOST=${HOST:-127.0.0.1:10443}
USER_=${USER_:-root}
PASS=${PASS:-root}

Xvfb :99 -screen 0 1400x900x24 >/tmp/xvfb.log 2>&1 &
XVFB=$!
trap 'kill $XVFB 2>/dev/null || true' EXIT
sleep 1

export DISPLAY=:99
export QT_QUICK_BACKEND=software
# shellcheck disable=SC2086
./build/src/router-console --connect "$HOST" --user "$USER_" --password "$PASS" ${EXTRA_ARGS---accept-new-cert} \
  >/tmp/router-console-gui.log 2>&1 &
APP=$!
sleep "${WAIT:-6}"

xdotool search --name "router-console" getwindowname %@ || true
# No window manager on the headless server, so the app window is not resized:
# capture the root and crop to the window's configured 1280x800 at 0,0.
import -display :99 -window root "$OUT"
convert "$OUT" -crop 1280x800+0+0 +repage "$OUT"
echo "screenshot: $OUT"

kill $APP 2>/dev/null || true
wait $APP 2>/dev/null || true
