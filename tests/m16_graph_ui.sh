#!/usr/bin/env bash
# Wiring check for the graph window: the interface window offers the four time
# ranges, asks the model for the one selected, and the graph component reads the
# store's grid (slot start + bucket width) instead of a fixed 120-point window.
set -euo pipefail
cd "${1:-$(dirname "$0")/..}"

fail() { echo "FAIL: $1" >&2; exit 1; }

for range in 1m 10m 1h 24h; do
    grep -q "{ id: \"$range\"" src/qml/InterfaceWindow.qml \
        || fail "InterfaceWindow.qml lost the $range range"
done

grep -q 'AppEngine.interfaces.history(ifName, rangeMs())' src/qml/InterfaceWindow.qml \
    || fail "InterfaceWindow.qml does not query the history store per range"
grep -q 'objectName: "range-"' src/qml/InterfaceWindow.qml \
    || fail "range buttons lost their object names"
grep -q 'property string initialRange' src/qml/InterfaceWindow.qml \
    || fail "InterfaceWindow.qml cannot be opened at a given range"

grep -q 'property int bucketMs' src/qml/TrafficGraph.qml \
    || fail "TrafficGraph.qml does not take a bucket width"
grep -q 'property real gridStartMs' src/qml/TrafficGraph.qml \
    || fail "TrafficGraph.qml does not take a grid start"
grep -q 'series\[i\] >= 0' src/qml/TrafficGraph.qml \
    || fail "TrafficGraph.qml does not break the line on gaps"

grep -q 'function openInterface(name, range)' src/qml/Main.qml \
    || fail "Main.qml openInterface lost its range argument"
grep -q 'initialRange: range' src/qml/Main.qml \
    || fail "Main.qml does not pass the startup range to the window"

grep -q 'models/TrafficHistory.cpp' src/CMakeLists.txt \
    || fail "TrafficHistory.cpp is not in the core library"

echo "m16-graph-ui: range selector, store query and gap-aware graph are wired"
