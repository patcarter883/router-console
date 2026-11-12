#!/usr/bin/env bash
# Requirement check for router auto-discovery: boot the dev VM, wait until it is
# up, then start the client's discovery path from cold and require the router in
# the discovered list within 10 s.
#
# Usage: tests/after-boot-discovery.sh <path-to-discovery-smoke> [seconds-after-boot]
set -euo pipefail
cd "$(dirname "$0")/.."

SMOKE=${1:-build/tests/discovery-smoke}
SETTLE=${2:-15}
VM=.vm
LOG=$(mktemp)

cleanup() { rm -f "$LOG"; }
trap cleanup EXIT

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
  echo "SKIP: qemu-system-x86_64 not installed"
  exit 3
fi

if ! ip link show rcnet-host >/dev/null 2>&1; then
  echo "SKIP: lab segment rcnet-host is absent (run scripts/dev-vm.sh once)"
  exit 3
fi

echo "== stopping any running dev VM"
for pid in $(pgrep -x qemu-system-x86 || true); do
  kill "$pid" 2>/dev/null || true
done
sleep 2

echo "== booting the dev VM"
(setsid nohup scripts/dev-vm.sh >"$VM/qemu.log" 2>&1 </dev/null &)

echo "== waiting for the router to answer ubus login"
for _ in $(seq 1 60); do
  sleep 2
  if curl -sk --max-time 3 https://127.0.0.1:10443/ubus \
      -d '{"jsonrpc":"2.0","id":1,"method":"call","params":["00000000000000000000000000000000","session","login",{"username":"root","password":"root"}]}' \
      2>/dev/null | grep -q '"ubus_rpc_session"'; then
    echo "router up"
    break
  fi
done

if ! curl -sk --max-time 3 https://127.0.0.1:10443/ubus \
    -d '{"jsonrpc":"2.0","id":1,"method":"call","params":["00000000000000000000000000000000","session","login",{"username":"root","password":"root"}]}' \
    2>/dev/null | grep -q '"ubus_rpc_session"'; then
  echo "SKIP: dev VM did not come up"
  exit 3
fi

echo "== letting the boot-time announcements pass (${SETTLE}s)"
sleep "$SETTLE"

echo "== clean client start: discovery must find the router within 10 s"
"$SMOKE" --hostname openwrt-dev --address 192.168.78.1 --timeout 10000
