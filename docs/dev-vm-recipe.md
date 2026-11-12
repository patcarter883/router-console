# Dev VM recipe — OpenWrt x86 (all development happens here)

Live routers are verification-only (star requirement). The video router
192.168.1.1 is production: PTP GM + chrony, never disturb UDP 319/320. The dev VM
therefore does **not** use OpenWrt's default 192.168.1.1 for its LAN — see below.

Everything after "Measured run" was executed on the CachyOS desktop on 2026-11-12
and is the authoritative version; the earlier draft lines that it corrects are
called out inline.

## Image

Newest published x86/64 ext4 combined-efi image (25.12.6 at the time of writing;
check the release index, `25.12.5` in the first draft was already stale):

    https://downloads.openwrt.org/releases/25.12.6/targets/x86/64/
    -> openwrt-25.12.6-x86-64-generic-ext4-combined-efi.img.gz

Verify against the release `sha256sums` before use. ext4 (not squashfs) so the
lab VM has no overlay limits.

## QEMU (this host has kvm)

The image is EFI-only, so it needs OVMF (Arch: `edk2-ovmf`, firmware at
`/usr/share/edk2/x64/`). Booting it with SeaBIOS fails — the first draft omitted
this. Convert the raw image to qcow2 so snapshots work:

    gunzip -k openwrt-25.12.6-x86-64-generic-ext4-combined-efi.img.gz
    qemu-img convert -O qcow2 openwrt-...-efi.img .vm/openwrt-dev.qcow2
    cp /usr/share/edk2/x64/OVMF_VARS.4m.fd .vm/OVMF_VARS.fd

`scripts/dev-vm.sh` boots it (serial console on `.vm/serial.sock`, driven by
`scripts/vm-console.sh 'command'`). Effective QEMU line:

    qemu-system-x86_64 -machine q35,accel=kvm -cpu host -m 512 -smp 2 \
      -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF_CODE.4m.fd \
      -drive if=pflash,format=raw,file=.vm/OVMF_VARS.fd \
      -drive file=.vm/openwrt-dev.qcow2,format=qcow2,if=none,id=disk \
      -device virtio-blk-pci,drive=disk,bootindex=1 \
      -netdev user,id=lan,net=192.168.77.0/24,host=192.168.77.2,dhcpstart=192.168.77.100,\
    hostfwd=tcp:127.0.0.1:10022-192.168.77.1:22,\
    hostfwd=tcp:127.0.0.1:10080-192.168.77.1:80,\
    hostfwd=tcp:127.0.0.1:10443-192.168.77.1:443 \
      -device virtio-net-pci,netdev=lan,bootindex=2 \
      -netdev user,id=wan -device virtio-net-pci,netdev=wan,bootindex=3 \
      -netdev socket,id=lan2,connect=127.0.0.1:15077 \
      -device virtio-net-pci,netdev=lan2 \
      -display none -serial unix:.vm/serial.sock,server=on,wait=off

Two corrections to the first draft, both measured:

- `-netdev user,id=n0,...` plus `-nic user,model=e1000` created **two** slirp
  networks and put the forwards on the NIC OpenWrt uses as WAN (eth1, DHCP
  10.0.2.15) while the guest's LAN lived on eth0 — so the forwards timed out.
  The forwards must point at the LAN-side guest address (192.168.77.1) on the
  same slirp segment as eth0. eth1 is a second, plain slirp net purely for
  outbound package downloads.
- `-m 512` is enough, but the ext4 rootfs of the combined image is only ~120 MB
  virtual; `apk add luci-base` + sqm fits (22.6 MiB of packages), heavier sets
  need `qemu-img resize`.
- Boot order is not `-boot order=c`: OVMF reads its own NVRAM `BootOrder`, and
  once a third NIC exists that order starts with PXE/HTTP boot, which cost
  **2-3 minutes** of DHCP/HTTP timeouts before the disk was tried. `-boot
  order=c` did not change it. A `bootindex=1` on `-device virtio-blk-pci`
  (plus 2/3 on the NICs) does: the guest reaches SSH in ~21 s.

## The discovery segment (why there is a third NIC)

Measured 2026-11-12: **slirp does not forward multicast.** With umdns running in
the guest and its announcements captured on `br-lan` inside the VM
(`192.168.77.1.5353 > 224.0.0.251.5353`, A + PTR records), `tcpdump -i any udp
port 5353` on the host saw 483 packets in 25 s — every one of them from the
host's own LAN (10.50.1.0/23) or docker bridges, none from 192.168.77.1. The
host has no interface on that slirp segment at all, so it cannot join
224.0.0.251 there.

A tap device is not available: this kernel has no `tun` module (`ip tuntap add`
fails with `open: No such device`, `modprobe tun` → module not found). veth
works (docker uses it), so the lab segment is a veth pair whose QEMU end is fed
by `scripts/mdns-l2-relay.py`, an L2 relay for QEMU's `-netdev socket` stream:

    guest eth2 192.168.78.1  <->  QEMU socket netdev  <->  relay  <->  rcnet-qemu ~~ rcnet-host 192.168.78.2/24

The relay forwards Ethernet frames in both directions (4-byte length prefix,
QEMU's stream framing), so the host kernel — and therefore a QUdpSocket joined
to 224.0.0.251 on `rcnet-host` — sees the guest's real mDNS packets, and the
guest answers ARP/ping on 192.168.78.1. `scripts/dev-vm.sh` creates the veth
(sudo), starts the relay (sudo, port 15077) and attaches the NIC; `DISCOVERY_LAN=0`
skips the whole thing when discovery is not being worked on.

`scripts/vm-discovery-setup.sh` provisions the guest side (idempotent, over SSH
on 10022): `apk add umdns`, `network.lan2` static 192.168.78.1/24 on eth2, lan2
in the lan firewall zone, lan2 in `umdns.@umdns[-1].network`, and the
`umdns-announce` timer described in
[ubus-rpcd-read-surface.md](ubus-rpcd-read-surface.md#router-auto-discovery-connect-window).


The guest LAN is renumbered to 192.168.77.1/24 so no address on this host can be
confused with the production router at 192.168.1.1:

    uci set network.lan.ipaddr='192.168.77.1/24' && uci commit network

## Bring-up checklist (measured)

1. Packages. 25.12 uses apk. **`luci-rpc` is not a package in 25.12** (LuCI was
   rewritten for ucode; the `luci-rpc` *ubus object* now ships inside
   `luci-base`, but the package of that name is gone). Corrected list:

       apk update
       apk add uhttpd uhttpd-mod-ubus rpcd rpcd-mod-file luci-base luci-mod-rpc \
               sqm-scripts luci-app-sqm tcpdump

   `luci-app-sqm` pulls `luci-base` anyway; `luci-mod-rpc` provides the
   `luci` ubus object methods LuCI itself calls.

2. Root password + SSH key (the console starts with no password set):

       printf 'root\nroot\n' | passwd root     # BusyBox warns "too short", still applies
       echo '<host pubkey>' >> /etc/dropbear/authorized_keys

   Password auth is enabled by default in 25.12 (`dropbear.main.PasswordAuth='on'`,
   `RootPasswordAuth='on'`); `ssh -p 10022 root@127.0.0.1` works either way.
   The password is only reachable through the 127.0.0.1 host forwards.

3. Enable and start the web/RPC stack:

       /etc/init.d/uhttpd enable && /etc/init.d/rpcd enable && /etc/init.d/uhttpd restart

   uhttpd generates a self-signed cert at `/etc/uhttpd.crt` on first start and
   listens on 0.0.0.0:80 and 0.0.0.0:443. That cert is why the client pins the
   router certificate fingerprint (see ubus-rpcd-read-surface.md, "Auth").

## Smoke test (the protocol contract)

    curl -k https://127.0.0.1:10443/ubus -d '{"jsonrpc":"2.0","id":1,"method":"call","params":["00000000000000000000000000000000","session","login",{"username":"root","password":"root"}]}'

Measured response (abridged; the real one also carries a large `acls` map):

    {"jsonrpc":"2.0","id":1,"result":[0,{"ubus_rpc_session":"510857bf223f6da5e91c9ca3a504e84c",
      "timeout":300,"expires":300,"acls":{...},"data":{"username":"root"}}]}

A second call carrying that token returns board data:

    curl -k https://127.0.0.1:10443/ubus -d '{"jsonrpc":"2.0","id":2,"method":"call","params":["<token>","system","board",{}]}'
    -> [0,{"kernel":"6.12.112","hostname":"OpenWrt","model":"QEMU Standard PC (Q35 + ICH9, 2009)",
          "board_name":"qemu-standard-pc-q35-ich9-2009","release":{"version":"25.12.6",
          "revision":"r33311-981c978eff","target":"x86/64",...}}]

`timeout` is 300 s: sessions expire, so the client re-logins on
`PERMISSION_DENIED` (ubus status 6) instead of surfacing an auth error.

## Client-side checks

With the VM up (`scripts/dev-vm.sh`):

    cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
    ctest --test-dir build --output-on-failure     # connect-smoke: login + system.board

`build/tests/connect-smoke` prints the typed board fields and exits 0 only when
the session and `system.board` both succeed:

    CONNECTED
      hostname: OpenWrt
      model:    QEMU Standard PC (Q35 + ICH9, 2009)
      firmware: OpenWrt 25.12.6 r33311-981c978eff
      kernel:   6.12.112
      target:   x86/64

The Qt window itself is checked with `scripts/gui-check.sh`, which runs the
client on a headless Xvfb, connects, and writes a screenshot
(`docs/evidence/m1-connect-window.png`). The certificate trust prompt is
captured the same way with `EXTRA_ARGS="" WAIT=5` after deleting
`~/.config/open-broadcast/router-console.conf`.

## Discovery checks

    ctest --test-dir build -LE vm-boot --output-on-failure   # connect + discovery
    ctest --test-dir build -L vm-boot --output-on-failure    # reboots the VM first

`build/tests/discovery-smoke` starts the same `AppEngine` the connect window
uses and exits 0 only when the router shows up in the discovered list:

    $ ./build/tests/discovery-smoke --hostname openwrt-dev --address 192.168.78.1 --timeout 10000
    DISCOVERED openwrt-dev 192.168.78.1:443 after 808 ms

The `vm-boot` test is the stated requirement — reboot, wait for ubus login, let
the boot-time announcements pass, then a clean client start:

    $ ctest --test-dir build -L vm-boot --output-on-failure
    == letting the boot-time announcements pass (15s)
    == clean client start: discovery must find the router within 10 s
    DISCOVERED openwrt-dev 192.168.78.1:443 after 3437 ms


## Traffic generator for graph checks

iperf3 in the VM (`apk add iperf3`) or from the host through the forwarded port.

## Custom packages

Build x86/64 .apk for luci-app-rist2rist via the OpenWrt SDK (same feed the
product uses — see the package Makefile in
open-broadcast/open-broadcast-product-codec/luci-app-rist2rist/), `apk add` them
in the VM. Same for luci-app-modemaim (cellular project) and nettime.

## Snapshots

Taken with the VM powered off (`qemu-img snapshot` must not run against a live
image):

    qemu-img snapshot -c clean-bringup .vm/openwrt-dev.qcow2
    qemu-img snapshot -l .vm/openwrt-dev.qcow2
    # ID 1  clean-bringup  0 B  2026-11-12 14:28:39

`clean-bringup` = 25.12.6 + packages above + password + SSH key + enabled
uhttpd/rpcd. Every destructive test (sysupgrade path especially) restores from
it: `qemu-img snapshot -a clean-bringup .vm/openwrt-dev.qcow2`.
