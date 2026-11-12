# router-console

A WinBox-parity native desktop client for our OpenWrt routers: core config domains
plus our custom LuCI apps, Qt 6 / C++20 / QML, Linux and Windows.

The finish line lives in the north star `router-console`:

    python3 ~/.hermes/skills/software-development/north-star/scripts/north_star.py show router-console

## Scope (settled with Pat, 2026-11-12)

- OpenWrt-only. The MikroTik ax² keeps WinBox; dual-protocol is a later extension.
- Feature parity with WinBox 4's core (connect window, left tree, dockable tool
  windows, live graphs, staged apply) + tool tier (ping, traceroute, port scan,
  torch-style per-host bandwidth, files + sysupgrade, SQM/CAKE stats) + first-class
  native views for OUR custom LuCI apps via a plugin mechanism.
- NOT: the general OpenWrt app ecosystem, macOS, mobile, embedding LuCI pages.
- Dev target: local OpenWrt x86 VM. Live routers are verification-only.

## Stack decision (measured, do not re-litigate)

WinBox 4 itself is Qt 6 + QML, statically linked C++ (GCC 11.5, Ubuntu), xcb/EGL/GL
direct — verified from the official `WinBox_Linux.zip` 4.5 binary on 2026-11-12
(see docs/winbox4-reference.md). The look-and-feel being replicated is Qt, so
Qt 6 is not "similar to WinBox", it is the same technology.

## Layout

- `src/` — Qt 6 / CMake application skeleton
- `docs/` — protocol notes, dev-VM recipe, WinBox 4 reference, plugin spec
- `AGENTS.md` — the brief for coding agents
