# Dev VM recipe — OpenWrt x86 (all development happens here)

Live routers are verification-only (star requirement). The video router
192.168.1.1 is production: PTP GM + chrony, never disturb UDP 319/320.

## Image

Download the x86/64 combined image from downloads.openwrt.org (current stable,
25.12.x line):

    https://downloads.openwrt.org/releases/25.12.5/targets/x86/64/
    -> openwrt-25.12.5-x86-64-generic-ext4-combined-efi.img.gz

(or squashfs-combined-efi; ext4 is easier for lab VMs — no overlay limits.)

## QEMU (this host has kvm)

    gunzip openwrt-...-efi.img.gz
    qemu-system-x86_64 -m 512 -smp 2 -enable-kvm \
      -drive file=openwrt.img,format=raw,if=virtio \
      -netdev user,id=n0,hostfwd=tcp::10022-:22,hostfwd=tcp::10080-:80,hostfwd=tcp::10443-:443 \
      -device virtio-net-pci,netdev=n0 \
      -nic user,model=e1000 -nographic

That gives one WAN-ish user-net iface + one extra for LAN-side experiments;
set the LAN side to a static/test subnet in UCI. SSH: `ssh -p 10022 root@127.0.0.1`.

## Bring-up checklist

1. `apk update && apk install uhttpd uhttpd-mod-ubus rpcd rpcd-mod-file
   luci-rpc luci-mod-rpc sqm-scripts luci-app-sqm tcpdump` (apk since 25.12).
2. Enable + start uhttpd/rpcd; confirm:
   `curl -k https://127.0.0.1:10443/ubus -d '{"jsonrpc":"2.0","id":1,"method":"call","params":["00000000000000000000000000000000","session","login",{"username":"root","password":"root"}]}'`
   -> a session token. That curl is the protocol smoke test.
3. Traffic generator for graph checks: iperf3 in the VM (apk add iperf3) or from
   the host through the forwarded port.
4. Custom packages: build x86/64 .apk for luci-app-rist2rist via the OpenWrt SDK
   (same feed the product uses — see the package Makefile in
   open-broadcast/open-broadcast-product-codec/luci-app-rist2rist/), `apk add`
   them in the VM. Same for luci-app-modemaim (cellular project) and nettime.

## Snapshots

`qemu-img snapshot -c clean-25.12.5 openwrt.img` after bring-up — every
destructive test (sysupgrade path especially) restores from it.
