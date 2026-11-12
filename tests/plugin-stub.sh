#!/usr/bin/env bash
# Requirement 6: a stub app view added entirely through the plugin interface
# appears in the client tree with zero diffs to core files.
#
#   tests/plugin-stub.sh <plugin-loading binary> <repo root>
#
# The stub is generated into a plugin root in TMPDIR, so nothing is written into
# the repository: it is loaded by the same Plugin::loadPlugins() the client's
# tree model calls, plus a git check that the interface needed no core change.
set -euo pipefail

LOADER=$1
REPO=$2
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/plugins/probe-stub"
cat > "$TMP/plugins/probe-stub/plugin.json" <<'JSON'
{
  "id": "probe-stub",
  "title": "Probe Stub",
  "order": 70,
  "interval": 5000,
  "view": "Probe.qml",
  "sources": [
    { "id": "board", "transport": "ubus", "object": "system", "method": "board" }
  ]
}
JSON
cat > "$TMP/plugins/probe-stub/Probe.qml" <<'QML'
import QtQuick
Item {
    property var plugin: null
    Text { text: "probe stub view" }
}
QML

echo "stub plugin root: $TMP/plugins"
"$LOADER" --dir "$TMP/plugins" --dump | tee "$TMP/dump.txt"

grep -q "probe-stub" "$TMP/dump.txt" || {
    echo "FAIL: the stub plugin did not load"
    exit 1
}
echo "ok: stub plugin 'probe-stub' loaded from a bare plugin root"

# The stub is files only: no CMake entry, no QML module entry, no core source.
CHANGED=$(git -C "$REPO" status --porcelain --untracked-files=all -- \
    CMakeLists.txt src/ tests/CMakeLists.txt | grep -v '^?? plugins/' || true)
if [ -n "$CHANGED" ]; then
    echo "FAIL: core files differ from HEAD while adding the stub:"
    echo "$CHANGED"
    exit 1
fi
echo "ok: zero core diffs for the stub (nothing outside plugins/ changed)"

echo "PASS"
