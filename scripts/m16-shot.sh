#!/usr/bin/env bash
# Persistent-graph evidence. Two client processes sample the same lab VM for WAIT
# seconds, one window opened at 10 m and one at 24 h, so each shot shows the
# history that accumulated while the client was connected. (That sampling
# continues with no window open at all is what tests/m16_traffic_history_live.cpp
# asserts.) Traffic is 8 MiB bursts through the VM's ssh forward, which is what
# moves the br-lan counters the client samples.
#
#   scripts/m16-shot.sh            # docs/evidence/m16-graphs{,-10m}.png
#   WAIT=420 scripts/m16-shot.sh   # a longer run for a fuller 24 h trace
#
# Display :102 is this milestone's headless X server (:98-:101 belong to other
# sessions); the screen is wide enough that both windows are fully visible, so a
# per-window capture cannot pick up the other one's pixels.
set -euo pipefail
cd "$(dirname "$0")/.."

HOST=${HOST:-127.0.0.1:60443}
SSHP=${SSHP:-60022}
DISPLAY_=${DISPLAY_:-:102}
WAIT=${WAIT:-240}
SIZE=${SIZE:-2600x1000}
OUT24=${OUT24:-docs/evidence/m16-graphs.png}
OUT24WIN=${OUT24WIN:-docs/evidence/m16-graphs-24h.png}
OUT10=${OUT10:-docs/evidence/m16-graphs-10m.png}
LOG=/tmp/router-console-m16
CROP=${CROP:-1280x800+0+0}

need_w=${SIZE%x*}
need_h=${SIZE#*x}
screen_w=0
if xdpyinfo -display "$DISPLAY_" >/dev/null 2>&1; then
    screen_w=$(xdpyinfo -display "$DISPLAY_" | awk '/dimensions/{split($2,a,"x"); print a[1]}')
fi
# Both windows have to be fully visible at once, so a display created earlier for
# a narrower screen is replaced (only ever this milestone's own :102).
if [ "${screen_w:-0}" -lt "$need_w" ]; then
    for pid in $(pgrep -x Xvfb); do
        if tr '\0' ' ' <"/proc/$pid/cmdline" | grep -q "^Xvfb ${DISPLAY_} "; then
            kill "$pid" 2>/dev/null || true
        fi
    done
    sleep 1
    Xvfb "$DISPLAY_" -screen 0 "${SIZE}x24" >/tmp/xvfb-m16.log 2>&1 &
    XVFB=$!
    trap 'kill $XVFB 2>/dev/null || true' EXIT
    sleep 1
fi

export DISPLAY=$DISPLAY_
export QT_QUICK_BACKEND=software
export RC_PAINT_STATS=1

(
    while :; do
        dd if=/dev/zero bs=1M count=8 2>/dev/null
        sleep 1
    done
) | ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -p "$SSHP" -T root@127.0.0.1 'cat > /dev/null' >/dev/null 2>&1 &
TRAFFIC=$!

launch() { # $1 = range, $2 = log suffix
    ./build/src/router-console --connect "$HOST" --user root --password root \
        --accept-new-cert --open-interface lan --interface-range "$1" \
        >"$LOG-$2.log" 2>&1 &
    echo $!
}

APP10=$(launch 10m 10m)
APP24=$(launch 24h 24h)
trap 'kill $APP10 $APP24 $TRAFFIC 2>/dev/null || true' EXIT

window_of() { # wid of the largest window owned by pid $1
    local pid=$1 wid best=0 bestw=0 w
    for wid in $(xdotool search --pid "$pid" 2>/dev/null); do
        w=$(xdotool getwindowgeometry "$wid" 2>/dev/null | awk '/Geometry/{split($2,a,"x"); print a[1]}')
        if [ -n "$w" ] && [ "$w" -gt "$bestw" ]; then best=$wid; bestw=$w; fi
    done
    echo "$best"
}

echo "sampling for ${WAIT}s, then capturing both windows"
sleep "$WAIT"

W10=$(window_of "$APP10")
W24=$(window_of "$APP24")
xdotool windowmove "$W10" 0 0
xdotool windowmove "$W24" 1300 0
sleep 2

import -display "$DISPLAY_" -window "$W10" "$OUT10"
magick "$OUT10" -crop "$CROP" +repage "$OUT10"
import -display "$DISPLAY_" -window "$W24" "$OUT24WIN"
magick "$OUT24WIN" -crop "$CROP" +repage "$OUT24WIN"

# The evidence shot: both windows at once, the 10 m detail next to the 24 h
# window that holds the same run.
import -display "$DISPLAY_" -window root "$OUT24"
magick "$OUT24" -crop "${need_w}x820+0+0" +repage "$OUT24"

echo "screenshots: $OUT10 $OUT24WIN $OUT24"
ls -l "$OUT10" "$OUT24WIN" "$OUT24"

echo "--- graph paint cost (last line per bucket width)"
grep '^rc-paint' "$LOG-10m.log" "$LOG-24h.log" | tail -4 || true

if grep -qiE "qrc:/.*error|TypeError|is not defined|Cannot assign|ReferenceError|Unable to assign" \
    "$LOG-10m.log" "$LOG-24h.log"; then
    echo "--- QML problems:"
    grep -iE "qrc:/.*error|TypeError|is not defined|Cannot assign|ReferenceError|Unable to assign" \
        "$LOG-10m.log" "$LOG-24h.log" | head -10
    exit 1
fi
