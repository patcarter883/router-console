#!/usr/bin/env bash
# Structure check for the MDI shell: the views are frame content, not top-level
# windows, and the manager is the only thing that opens them. Catches the
# regression where a view quietly goes back to being its own OS window.
set -euo pipefail
cd "${1:-$(dirname "$0")/..}"

fail() { echo "FAIL: $1" >&2; exit 1; }

for f in src/qml/*Window.qml; do
    grep -qE '^(Window|ApplicationWindow) \{' "$f" && fail "$f is still a top-level window"
    grep -q '^Rectangle {' "$f" || fail "$f does not have an Item-based root"
    grep -q 'property string windowTitle' "$f" || fail "$f does not expose windowTitle"
    grep -q 'property int minimumWidth' "$f" || fail "$f does not expose minimumWidth"
    grep -qE '^\s+visible: true' "$f" && fail "$f still forces visible: true"
    grep -q 'onClosing' "$f" && fail "$f still relies on Window.onClosing"
done

grep -q 'qml/MdiWindowFrame.qml' src/CMakeLists.txt || fail "MdiWindowFrame.qml is not in the QML module"
grep -q 'ui/MdiGeometry.cpp' src/CMakeLists.txt || fail "MdiGeometry.cpp is not in the core library"

grep -q 'MdiWindowFrame {}' src/qml/Main.qml || fail "Main.qml does not create MdiWindowFrame"
grep -q 'MdiGeometry.cascade' src/qml/Main.qml || fail "Main.qml does not use cascade layout"
grep -q 'MdiGeometry.tile' src/qml/Main.qml || fail "Main.qml does not use tile layout"
grep -q 'function closeAllWindows' src/qml/Main.qml || fail "Main.qml has no close-all"
grep -q 'MdiGeometry.save' src/qml/MdiWindowFrame.qml || fail "frames do not persist geometry"
grep -q 'MdiGeometry.load' src/qml/MdiWindowFrame.qml || fail "frames do not restore geometry"

# The tree stays the launcher: every view kind is still reachable from it.
for opener in openInterface openConfig openTool openRouting openPlugin; do
    grep -q "function $opener" src/qml/Main.qml || fail "Main.qml lost $opener"
done

echo "m15-mdi-structure: views are frame content, launcher and manager wiring present"
