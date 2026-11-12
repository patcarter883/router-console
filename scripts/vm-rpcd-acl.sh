#!/usr/bin/env bash
# Install the client's rpcd ACL group on the dev VM. The stock LuCI groups do not
# grant `uci revert` (see docs/ubus-rpcd-read-surface.md), so the config editor
# needs this group to discard staged changes. Idempotent.
#
#   SSHP=10022 scripts/vm-rpcd-acl.sh
set -euo pipefail
cd "$(dirname "$0")/.."

SSHP=${SSHP:-10022}
SSHOPTS=(-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
         -o ConnectTimeout=5 -p "$SSHP")

# The VM has no sftp-server, so pipe the file instead of scp.
ssh "${SSHOPTS[@]}" root@127.0.0.1 \
    'cat > /usr/share/rpcd/acl.d/router-console.json; /etc/init.d/rpcd restart >/dev/null 2>&1' \
    < scripts/router-console.acl.json

sleep 2
ssh "${SSHOPTS[@]}" root@127.0.0.1 \
    'ubus call uci get "{\"config\":\"sqm\",\"type\":\"queue\"}" >/dev/null &&
     ubus call uci revert "{\"config\":\"sqm\"}" >/dev/null &&
     echo "rpcd ACL group router-console installed: uci get + revert allowed"'
