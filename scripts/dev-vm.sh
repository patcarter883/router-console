#!/usr/bin/env bash
# Boot the dev OpenWrt VM. Console: .vm/serial.sock (scripts/vm-console.sh).
# eth0 = br-lan 192.168.77.1 (slirp LAN; host-forwards 127.0.0.1:10022/10080/10443)
# eth1 = wan, DHCP from slirp (10.0.2.15) for package downloads.
# eth2 = lan2 192.168.78.1 on a veth segment relayed to the host kernel by
#        scripts/mdns-l2-relay.py: slirp drops multicast and this kernel has no
#        tun module, so mDNS discovery is measured over this segment
#        (host end rcnet-host, 192.168.78.2/24).
set -euo pipefail
cd "$(dirname "$0")/.."
VM=.vm
VETH_QEMU=${VETH_QEMU:-rcnet-qemu}      # QEMU side of the segment
VETH_HOST=${VETH_HOST:-rcnet-host}      # host side, carries 192.168.78.2/24
RELAY_PORT=${RELAY_PORT:-15077}
RELAY_PID="$VM/mdns-relay.pid"

[ -f "$VM/OVMF_VARS.fd" ] || cp /usr/share/edk2/x64/OVMF_VARS.4m.fd "$VM/OVMF_VARS.fd"

lan2_args=()
if [ "${DISCOVERY_LAN:-1}" = 1 ]; then
  if ! ip link show "$VETH_QEMU" >/dev/null 2>&1; then
    sudo ip link add "$VETH_QEMU" type veth peer name "$VETH_HOST"
    sudo ip addr add 192.168.78.2/24 dev "$VETH_HOST"
  fi
  sudo ip link set "$VETH_HOST" up
  sudo ip link set "$VETH_QEMU" up
  if ! ss -ltn | grep -q ":$RELAY_PORT "; then
    sudo python3 scripts/mdns-l2-relay.py --iface "$VETH_QEMU" \
      --listen "127.0.0.1:$RELAY_PORT" >"$VM/mdns-relay.log" 2>&1 &
    echo $! >"$RELAY_PID"
    sleep 0.5
  fi
  lan2_args=(-netdev "socket,id=lan2,connect=127.0.0.1:$RELAY_PORT"
             -device virtio-net-pci,netdev=lan2)
else
  echo "dev-vm: no discovery segment; mDNS cannot reach the host over slirp" >&2
fi

exec qemu-system-x86_64 \
  -machine q35,accel=kvm -cpu host -m 512 -smp 2 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd \
  -drive if=pflash,format=raw,file="$VM/OVMF_VARS.fd" \
  -drive file="$VM/openwrt-dev.qcow2",format=qcow2,if=none,id=disk \
  -device virtio-blk-pci,drive=disk,bootindex=1 \
  -netdev user,id=lan,net=192.168.77.0/24,host=192.168.77.2,dhcpstart=192.168.77.100,hostfwd=tcp:127.0.0.1:10022-192.168.77.1:22,hostfwd=tcp:127.0.0.1:10080-192.168.77.1:80,hostfwd=tcp:127.0.0.1:10443-192.168.77.1:443 \
  -device virtio-net-pci,netdev=lan,bootindex=2 \
  -netdev user,id=wan \
  -device virtio-net-pci,netdev=wan,bootindex=3 \
  "${lan2_args[@]}" \
  -display none \
  -serial unix:"$VM/serial.sock",server=on,wait=off \
  "$@"
