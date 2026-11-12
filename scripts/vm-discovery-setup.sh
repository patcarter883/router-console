#!/usr/bin/env bash
# Provision the dev VM for router auto-discovery (idempotent). Run against a
# booted VM: scripts/vm-discovery-setup.sh
#
# What it does, and why (measured — see docs/dev-vm-recipe.md):
#  - umdns announces the router's records (_services._dns-sd._udp.local PTR and
#    <hostname>.local A) once at start-up, does not answer queries and does not
#    re-announce, so a client starting later would never see it.
#  - eth2/lan2 (192.168.78.1) is the segment that reaches the host with
#    multicast intact; the slirp LAN drops it.
#  - /etc/init.d/umdns-announce restarts umdns every 5 s so announcements recur.
set -euo pipefail
cd "$(dirname "$0")/.."
PORT=${PORT:-10022}
SSH=(ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p "$PORT" root@127.0.0.1)

"${SSH[@]}" 'sh -s' <<'REMOTE'
set -e
apk update >/dev/null 2>&1 || true
apk add umdns >/dev/null 2>&1 || true

uci -q get network.lan2 >/dev/null 2>&1 || uci set network.lan2=interface
uci set network.lan2.device=eth2
uci set network.lan2.proto=static
uci set network.lan2.ipaddr=192.168.78.1/24
uci set network.lan2.ip6assign=0
uci commit network

if ! uci -q get firewall.@zone[0].network | grep -qw lan2; then
	uci add_list firewall.@zone[0].network=lan2
	uci commit firewall
fi

if ! uci -q get umdns.@umdns[-1].network | grep -qw lan2; then
	uci add_list umdns.@umdns[-1].network=lan2
	uci commit umdns
fi

cat > /etc/init.d/umdns-announce <<'EOF'
#!/bin/sh /etc/rc.common
# umdns announces only at start-up; re-start it on a timer so a client that
# starts after boot still discovers the router.
START=96
USE_PROCD=1

start_service() {
	procd_open_instance
	procd_set_param command /bin/sh -c "while :; do sleep 5; /etc/init.d/umdns restart >/dev/null 2>&1; done"
	procd_set_param respawn
	procd_close_instance
}
EOF
chmod +x /etc/init.d/umdns-announce
/etc/init.d/umdns-announce enable
/etc/init.d/network reload >/dev/null 2>&1 || true
/etc/init.d/firewall reload >/dev/null 2>&1 || true
/etc/init.d/umdns enable
/etc/init.d/umdns restart
/etc/init.d/umdns-announce restart
ip -4 addr show eth2 | grep -q 192.168.78.1 && echo "lan2 up"
echo "umdns networks: $(uci -q get umdns.@umdns[-1].network)"
REMOTE
