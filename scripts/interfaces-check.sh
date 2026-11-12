#!/usr/bin/env bash
# Headless check for the Interfaces view: connect to the dev VM, open the graph
# window for one interface, push traffic through it and capture the main window
# and the graph window 4 s apart. Used as the visual half of the graphs check.
#
#   HOST=127.0.0.1:10443 SSHP=10022 INTERFACE=lan scripts/interfaces-check.sh
set -euo pipefail
cd "$(dirname "$0")/.."

HOST=${HOST:-127.0.0.1:10443}
SSHP=${SSHP:-10022}
INTERFACE=${INTERFACE:-lan}
OUT=${OUT:-/tmp/router-console-interfaces}
DISPLAY_NUM=${DISPLAY_NUM:-95}
WAIT=${WAIT:-20}

rm -f "$OUT"-*.png
Xvfb ":$DISPLAY_NUM" -screen 0 1600x900x24 > "/tmp/xvfb$DISPLAY_NUM.log" 2>&1 &
XVFB=$!
cleanup() { kill $TRAFFIC $APP $WM $XVFB 2>/dev/null || true; }
trap cleanup EXIT
sleep 1

export DISPLAY=":$DISPLAY_NUM"
export QT_QUICK_BACKEND=software
openbox > "/tmp/openbox$DISPLAY_NUM.log" 2>&1 &
WM=$!
sleep 1

# Traffic from inside the VM, discarded on the host: only router counters move.
ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o ConnectTimeout=5 -p "$SSHP" root@127.0.0.1 'cat /dev/zero' > /dev/null 2>&1 &
TRAFFIC=$!
sleep 1

./build/src/router-console --connect "$HOST" --user root --password root \
    --accept-new-cert --open-interface "$INTERFACE" > /tmp/router-console-interfaces.log 2>&1 &
APP=$!
sleep "$WAIT"

MAIN=$(xdotool search --name '^router-console$' | tail -1)
GRAPH=$(xdotool search --name "^Interface $INTERFACE" | tail -1)
[ -n "$MAIN" ] && import -display ":$DISPLAY_NUM" -window "$MAIN" "$OUT-main.png"
if [ -n "$GRAPH" ]; then
    import -display ":$DISPLAY_NUM" -window "$GRAPH" "$OUT-graph-1.png"
    sleep 4
    import -display ":$DISPLAY_NUM" -window "$GRAPH" "$OUT-graph-2.png"
    echo "graph window captured: $OUT-graph-1.png $OUT-graph-2.png"
else
    echo "no graph window for interface $INTERFACE" >&2
fi
[ -n "$MAIN" ] && echo "main window captured: $OUT-main.png"
