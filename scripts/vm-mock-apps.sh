#!/usr/bin/env bash
# Install the custom-app fixtures the milestone-6 plugin views bind to, on the
# dev VM. Honest scope: only rist2rist's service registration and the two rpcd
# status objects are mocked (no LuCI package, no real daemon, no modem, no
# GstNetTime provider) — the client reads the same JSON shapes the real apps
# produce. towers.json is a small extract with the real schema.
#
#   SSHP=10022 scripts/vm-mock-apps.sh
set -euo pipefail
cd "$(dirname "$0")/.."

SSHP=${SSHP:-10022}
SSHOPTS=(-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
         -o ConnectTimeout=5 -p "$SSHP")
VM="root@127.0.0.1"

ssh_vm() { ssh "${SSHOPTS[@]}" "$VM" "$@"; }
put() { ssh_vm "cat > '$2'" < "scripts/vm-fixtures/$1"; }

ssh_vm 'mkdir -p /usr/libexec/rpcd /www/luci-static/resources/modemaim /etc/config'

put etc/config/rist2rist /etc/config/rist2rist
put www/luci-static/resources/modemaim/towers.json /www/luci-static/resources/modemaim/towers.json
put rpcd/modemaim /usr/libexec/rpcd/modemaim
put rpcd/nettime /usr/libexec/rpcd/nettime
put init.d/rist2rist /etc/init.d/rist2rist
put init.d/nettime /etc/init.d/nettime

ssh_vm 'chmod +x /usr/libexec/rpcd/modemaim /usr/libexec/rpcd/nettime \
        /etc/init.d/rist2rist /etc/init.d/nettime;
        /etc/init.d/rist2rist enable; /etc/init.d/rist2rist restart;
        /etc/init.d/nettime enable; /etc/init.d/nettime restart;
        /etc/init.d/rpcd restart >/dev/null 2>&1'
sleep 2

echo "--- service registration"
ssh_vm 'ubus call service list "{\"name\":\"rist2rist\"}" | head -20'
echo "--- modemaim status"
ssh_vm 'ubus call modemaim status'
echo "--- modemaim origin"
ssh_vm 'ubus call modemaim origin'
echo "--- nettime status"
ssh_vm 'ubus call nettime status'
echo "--- rist2rist config"
ssh_vm 'ubus call uci get "{\"config\":\"rist2rist\"}" | head -12'
echo "--- towers.json served"
ssh_vm 'wget -q -O - http://127.0.0.1/luci-static/resources/modemaim/towers.json | head -c 160; echo'
