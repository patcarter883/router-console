#!/usr/bin/env bash
# Run one command on the dev VM's serial console and print its output.
# Usage: scripts/vm-console.sh 'uci show network'
set -euo pipefail
cd "$(dirname "$0")/.."
cmd="$*"
out=$(mktemp)
{
  sleep 0.3
  printf '%s\r\n' "$cmd"
  printf 'echo __RC_DONE_%s__\r\n' "$$"
  sleep "${RC_WAIT:-4}"
} | timeout "$(( ${RC_WAIT:-4} + 8 ))" socat - UNIX-CONNECT:.vm/serial.sock > "$out" || true
# strip echo of the command and read until the sentinel
sed -e 's/\r$//' "$out" | awk -v c="$cmd" '
  index($0, c) == 1 { next }
  /__RC_DONE_/ { exit }
  { print }
'
rm -f "$out"
