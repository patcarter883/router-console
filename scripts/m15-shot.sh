#!/usr/bin/env bash
# Capture the MDI shell on a headless X server (display :101; :98/:99/:100
# belong to other sessions). Four views open at once — live interface graph,
# routes, log and terminal — while a host->VM stream keeps the counters moving.
#
#   scripts/m15-shot.sh multi OUT=docs/evidence/m15-mdi-multi.png
#   scripts/m15-shot.sh tile  OUT=docs/evidence/m15-tile.png
#   scripts/m15-shot.sh restored OUT=/tmp/m15-restored.png
#
# multi:    press Tile, then drag the Terminal frame by its title bar so it
#           overlaps the route table (title-bar drag and z-order, all bodies
#           still readable).
# tile:     open six views as a cascade and press the Tile toolbar button.
# restored: same views, no layout — the geometry the previous run persisted.
set -euo pipefail
cd "$(dirname "$0")/.."

MODE=${1:-multi}
OUT=${OUT:-/tmp/m15-$MODE.png}
HOST=${HOST:-127.0.0.1:60443}
SSHP=${SSHP:-60022}
DISPLAY_=${DISPLAY_:-:101}
WAIT=${WAIT:-12}
SIZE=${SIZE:-1400x900}

WINDOWS=(--open-interface br-lan --open-routing routes --open-tool log --open-tool terminal)
DRAG_FROM=""

case "$MODE" in
    multi)
        CLICK_TILE=1
        DRAG_FROM=${DRAG_FROM:-900,422}
        DRAG_TO=${DRAG_TO:-820,332}
        ;;
    tile)
        WINDOWS+=(--open-routing ospf --open-tool torch)
        CLICK_TILE=1
        ;;
    restored)
        WINDOWS+=(--open-routing ospf --open-tool torch)
        ;;
esac

if ! xdpyinfo -display "$DISPLAY_" >/dev/null 2>&1; then
    Xvfb "$DISPLAY_" -screen 0 "${SIZE}x24" >/tmp/xvfb-m15.log 2>&1 &
    XVFB=$!
    trap 'kill $XVFB 2>/dev/null || true' EXIT
    sleep 1
fi

export DISPLAY=$DISPLAY_
export QT_QUICK_BACKEND=software

# Traffic over br-lan: bursts into the VM's ssh forward give the interface graph
# a sawtooth instead of one short spike.
(
    while :; do
        dd if=/dev/zero bs=1M count=24 2>/dev/null
        sleep 1
    done
) | ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -p "$SSHP" -T root@127.0.0.1 'cat > /dev/null' >/dev/null 2>&1 &
TRAFFIC=$!
trap 'kill $TRAFFIC 2>/dev/null || true' EXIT

./build/src/router-console --connect "$HOST" --user root --password root --accept-new-cert \
    "${WINDOWS[@]}" >"/tmp/router-console-m15-$MODE.log" 2>&1 &
APP=$!
trap 'kill $APP 2>/dev/null || true; kill $TRAFFIC 2>/dev/null || true' EXIT
sleep "$WAIT"

# Tile button in the header toolbar.
if [ -n "${CLICK_TILE:-}" ]; then
    xdotool mousemove 978 20 click 1
    sleep 3
fi

if [ -n "$DRAG_FROM" ]; then
    X1=${DRAG_FROM%,*}; Y1=${DRAG_FROM#*,}
    X2=${DRAG_TO%,*};   Y2=${DRAG_TO#*,}
    xdotool mousemove "$X1" "$Y1" mousedown 1
    for step in 1 2 3 4 5; do
        xdotool mousemove $(( X1 + (X2 - X1) * step / 5 )) $(( Y1 + (Y2 - Y1) * step / 5 ))
        sleep 0.1
    done
    xdotool mouseup 1
    sleep 1
fi

if [ -n "${TYPE_CMD:-}" ]; then
    WID=$(xdotool search --name "router-console" | head -1)
    xdotool windowfocus "$WID" || true
    sleep 0.3
    xdotool mousemove "${CLICK_X:-940}" "${CLICK_Y:-657}" click 1
    sleep 1
    xdotool type --delay 40 "$TYPE_CMD"
    xdotool key Return
    sleep 4
fi

import -display "$DISPLAY_" -window root "$OUT"
magick "$OUT" -crop 1280x800+0+0 +repage "$OUT"
echo "screenshot: $OUT"
ls -l "$OUT"

if grep -qiE "qrc:/.*error|TypeError|is not defined|Cannot assign|ReferenceError|Unable to assign" \
    "/tmp/router-console-m15-$MODE.log"; then
    echo "--- QML problems:"
    grep -iE "qrc:/.*error|TypeError|is not defined|Cannot assign|ReferenceError|Unable to assign" \
        "/tmp/router-console-m15-$MODE.log" | head -10
    exit 1
fi
