#!/usr/bin/env bash
# Headless check for the config editor: connect to the dev VM, open one domain
# editor, stage a single option change and capture the window. Nothing is
# applied, so the router config is untouched; the capture shows the staged row
# in the pending-changes list.
#
#   HOST=127.0.0.1:10443 DOMAIN=sqm STAGE=download=91000 scripts/config-check.sh
set -euo pipefail
cd "$(dirname "$0")/.."

HOST=${HOST:-127.0.0.1:10443}
DOMAIN=${DOMAIN:-sqm}
STAGE=${STAGE:-download=91000}
OUT=${OUT:-/tmp/router-console-config}
DISPLAY_NUM=${DISPLAY_NUM:-96}
WAIT=${WAIT:-18}

rm -f "$OUT"-*.png
Xvfb ":$DISPLAY_NUM" -screen 0 1600x900x24 > "/tmp/xvfb$DISPLAY_NUM.log" 2>&1 &
XVFB=$!
cleanup() { kill $APP $WM $XVFB 2>/dev/null || true; }
trap cleanup EXIT
sleep 1

export DISPLAY=":$DISPLAY_NUM"
export QT_QUICK_BACKEND=software
openbox > "/tmp/openbox$DISPLAY_NUM.log" 2>&1 &
WM=$!
sleep 1

./build/src/router-console --connect "$HOST" --user root --password root \
    --accept-new-cert --open-config "$DOMAIN" --stage "$STAGE" \
    > /tmp/router-console-config.log 2>&1 &
APP=$!
sleep "$WAIT"

MAIN=$(xdotool search --name '^router-console$' | tail -1)
EDITOR=$(xdotool search --name '^Config' | tail -1)
if [ -n "$EDITOR" ]; then
    xdotool windowactivate "$EDITOR"
    sleep 2
    import -display ":$DISPLAY_NUM" -window "$EDITOR" "$OUT-editor.png"
    echo "editor window captured: $OUT-editor.png"
else
    echo "no config editor window (see /tmp/router-console-config.log)" >&2
    exit 1
fi
# The main window is behind the editor; raise it and give the software renderer
# a couple of frames before grabbing.
if [ -n "$MAIN" ]; then
    xdotool windowactivate "$MAIN"
    sleep 3
    import -display ":$DISPLAY_NUM" -window "$MAIN" "$OUT-main.png"
    echo "main window captured: $OUT-main.png"
fi
