# router-console

A WinBox-parity native desktop client for OpenWrt routers, built with Qt 6 / C++20 / QML,
running on Linux and Windows. The reference UX is MikroTik WinBox 4 (itself measured to be a
Qt 6/QML application): connect window with router auto-discovery, left config tree, dockable
tool windows, live traffic graphs, staged configuration changes.

## Features

- **Connect + discover** — rpcd session login over HTTPS with certificate trust-on-first-use
  and pinning; routers announce themselves via mDNS/umdns and appear in the connect window.
- **Live interfaces** — per-interface RX/TX graphs at ≥1 Hz, driven by `network.device.status`.
- **Staged config editing** — WinBox-style pending-changes list over UCI's own
  `changes`/`apply`/`confirm`/`revert` semantics. Domains (SQM, system, network, ...) are data
  in a registry, not hard-coded pages.
- **Tools** — ping, traceroute, port scan, torch-style live per-host bandwidth, file manager
  with sysupgrade, SQM/CAKE qdisc stats.
- **Plugins** — first-class native views for custom LuCI apps, added via a plugin manifest
  without touching the client core.

The protocol layer speaks only the OpenWrt ubus/rpcd surface (JSON-RPC over uhttpd) and hands
the rest of the app typed C++ structs — no JSON above the protocol boundary.

## Building

Requires Qt 6.4+ and CMake 3.24+.

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build

Integration tests (`ctest`) run against a local OpenWrt x86 dev VM and skip themselves when
it is not present — see `docs/dev-vm-recipe.md`.

## License

AGPL-3.0-or-later — see `LICENSE`.
