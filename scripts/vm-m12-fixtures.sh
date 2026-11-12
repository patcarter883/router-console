#!/usr/bin/env bash
# Milestone 12 lab fixture: one `config host` reservation in /etc/config/dhcp so
# the DHCP lease window has a reserved row on the dev VM.
#
# The VM's LAN segment has no DHCP clients: a DISCOVER sent from br-lan is
# answered by QEMU's slirp DHCP (192.168.77.2) before dnsmasq sees it, so
# /tmp/dhcp.leases stays empty (measured with udhcpc -i br-lan). Reservations are
# therefore the rows this VM can be asserted on; scripts/vm-m12-fixtures.sh
# installs one, and the ctest asserts it reads back through the client.
#
#   SSHP=20022 scripts/vm-m12-fixtures.sh
set -euo pipefail
cd "$(dirname "$0")/.."

SSHP=${SSHP:-10022}
SSHOPTS=(-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
         -o ConnectTimeout=5 -p "$SSHP")

ssh "${SSHOPTS[@]}" root@127.0.0.1 '
set -e
uci -q delete dhcp.rcm12lab || true
uci set dhcp.rcm12lab=host
uci set dhcp.rcm12lab.name=rc-m12-reserved
uci set dhcp.rcm12lab.ip=192.168.77.240
uci -q delete dhcp.rcm12lab.mac || true
uci add_list dhcp.rcm12lab.mac=52:54:00:12:34:56
uci set dhcp.rcm12lab.leasetime=12h
uci commit dhcp
ubus call uci get "{\"config\":\"dhcp\",\"type\":\"host\"}"
'
