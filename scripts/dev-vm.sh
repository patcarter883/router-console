#!/usr/bin/env bash
# Boot the dev OpenWrt VM. Console: .vm/serial.sock (scripts/vm-console.sh).
# eth0 = br-lan 192.168.77.1 (slirp LAN; host-forwards 127.0.0.1:10022/10080/10443)
# eth1 = wan, DHCP from slirp (10.0.2.15) for package downloads.
set -euo pipefail
cd "$(dirname "$0")/.."
VM=.vm
[ -f "$VM/OVMF_VARS.fd" ] || cp /usr/share/edk2/x64/OVMF_VARS.4m.fd "$VM/OVMF_VARS.fd"

exec qemu-system-x86_64 \
  -machine q35,accel=kvm -cpu host -m 512 -smp 2 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd \
  -drive if=pflash,format=raw,file="$VM/OVMF_VARS.fd" \
  -drive file="$VM/openwrt-dev.qcow2",format=qcow2,if=virtio \
  -netdev user,id=lan,net=192.168.77.0/24,host=192.168.77.2,dhcpstart=192.168.77.100,hostfwd=tcp:127.0.0.1:10022-192.168.77.1:22,hostfwd=tcp:127.0.0.1:10080-192.168.77.1:80,hostfwd=tcp:127.0.0.1:10443-192.168.77.1:443 \
  -device virtio-net-pci,netdev=lan \
  -netdev user,id=wan \
  -device virtio-net-pci,netdev=wan \
  -display none \
  -serial unix:"$VM/serial.sock",server=on,wait=off \
  "$@"
