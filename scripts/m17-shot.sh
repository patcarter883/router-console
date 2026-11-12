#!/usr/bin/env bash
# Milestone-17 evidence: the modemaim site map and the aiming view, both open
# against the lab VM through their plugin windows, with a live signal on the
# fast lane. The aim shot waits for readings to accumulate, types a bearing into
# the mark field and presses Enter — the same path the operator's Mark button
# takes — so the shot shows a real manual mark drawn on the rose.
#
#   RC_TEST_HOST=127.0.0.1:64443 RC_TEST_SSH_PORT=64422 scripts/m17-shot.sh
#
# Display :103 only (:98-:102 belong to other sessions); exits 3 without a
# reachable router, which is what the ctest entry treats as a skip.
set -euo pipefail
cd "$(dirname "$0")/.."

HOST=${RC_TEST_HOST:-127.0.0.1:10443}
SSHP=${RC_TEST_SSH_PORT:-10022}
DISPLAY_=${DISPLAY_:-:103}
SIZE=${SIZE:-1400x900}
WAIT=${WAIT:-16}
OUT_MAP=${OUT_MAP:-docs/evidence/m17-map.png}
OUT_AIM=${OUT_AIM:-docs/evidence/m17-aimview.png}
LOG=/tmp/router-console-m17
BEARING=${BEARING:-45}

if ! timeout 3 bash -c "exec 3<>/dev/tcp/127.0.0.1/${SSHP}" 2>/dev/null; then
    echo "SKIP: no dev VM ssh forward on 127.0.0.1:${SSHP}"
    exit 3
fi

if ! xdpyinfo -display "$DISPLAY_" >/dev/null 2>&1; then
    Xvfb "$DISPLAY_" -screen 0 "${SIZE}x24" >/tmp/xvfb-m17.log 2>&1 &
    XVFB=$!
    trap 'kill $XVFB 2>/dev/null || true' EXIT
    sleep 1
fi

export DISPLAY=$DISPLAY_
export QT_QUICK_BACKEND=software
export QT_FORCE_STDERR_LOGGING=1
export QT_ASSUME_STDERR_HAS_CONSOLE=1

launch() { # $1 = plugin id, $2 = log suffix
    ./build/src/router-console --connect "$HOST" --user root --password root \
        --accept-new-cert --open-plugin "$1" >"$LOG-$2.log" 2>&1 &
    echo $!
}

APP_MAP=$(launch modemaim-map map)
APP_AIM=$(launch modemaim-aim aim)
trap 'kill $APP_MAP $APP_AIM ${XVFB:-} 2>/dev/null || true' EXIT

window_of() { # wid of the largest window owned by pid $1
    local pid=$1 wid best=0 bestw=0 w
    for wid in $(xdotool search --pid "$pid" 2>/dev/null); do
        w=$(xdotool getwindowgeometry "$wid" 2>/dev/null | awk '/Geometry/{split($2,a,"x"); print a[1]}')
        if [ -n "$w" ] && [ "$w" -gt "$bestw" ]; then best=$wid; bestw=$w; fi
    done
    echo "$best"
}

echo "waiting ${WAIT}s for the extract and the signal lane"
sleep "$WAIT"

W_MAP=$(window_of "$APP_MAP")
W_AIM=$(window_of "$APP_AIM")
if [ -z "$W_MAP" ] || [ -z "$W_AIM" ]; then
    echo "FAILED: no client window (map=$W_MAP aim=$W_AIM)"
    exit 1
fi

xdotool windowmove "$W_MAP" 0 0
xdotool windowmove "$W_AIM" 0 0
sleep 1

# No compositor on this display: raise a window before capturing it, or the
# other one's pixels are what comes back.

# Aim window last so it holds X input focus (there is no window manager on this
# display, so focus is set directly); type the bearing and press Enter.
xdotool windowraise "$W_AIM"
xdotool windowfocus --sync "$W_AIM" 2>/dev/null || xdotool windowfocus "$W_AIM" || true
sleep 1
xdotool type --delay 60 "$BEARING" || xdotool type --window "$W_AIM" --delay 60 "$BEARING"
xdotool key Return || xdotool key --window "$W_AIM" Return
sleep 2

import -display "$DISPLAY_" -window "$W_AIM" "$OUT_AIM"
xdotool windowraise "$W_MAP"
sleep 1
import -display "$DISPLAY_" -window "$W_MAP" "$OUT_MAP"

echo "screenshots: $OUT_AIM $OUT_MAP"
ls -l "$OUT_AIM" "$OUT_MAP"

status=0
for log in "$LOG-aim.log" "$LOG-map.log"; do
    if grep -qiE "qrc:/.*error|TypeError|is not defined|Cannot assign|ReferenceError|Unable to assign|Unable to assign" "$log"; then
        echo "--- QML problems in $log:"
        grep -iE "qrc:/.*error|TypeError|is not defined|Cannot assign|ReferenceError|Unable to assign" "$log" | head -10
        status=1
    fi
done
[ "$status" -eq 0 ] && echo "no QML errors in either plugin window"
exit "$status"
