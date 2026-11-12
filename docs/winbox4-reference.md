# WinBox 4 — measured reference (2026-11-12)

Source of record for what we are replicating. All claims below were measured from
the official `WinBox_Linux.zip` (4.5) downloaded from download.mikrotik.com; scratch
binary at ~/.hermes/cache/scratch/winbox45/WinBox.

## Technology (measured from the ELF)

- Qt 6 + QML: `.qml_compile_hash` section and `.qtmimedatabase` strings in the binary.
- C++ (libstdc++ symbols: `std::pmr::monotonic_buffer_resource`, `std::bad_function_call`).
- GCC: `(Ubuntu 11.5.0-10ubuntu1~20~ppa3) 11.5.0`.
- Statically linked Qt: `ldd` shows NO libQt*.so; direct deps are
  libxcb-*, libxkbcommon(-x11), libEGL, libGL, libfreetype, libfontconfig, libdbus.
- NOT Electron/Tauri/CEF: no Chromium, node, or webview markers anywhere.
- Stripped ELF x86-64 PIE, "for GNU/Linux 3.2.0".

## UX elements to mirror (the parity surface)

- Loader/connect window: address + credentials, remembered hosts.
- Main window: left tree of every config domain; items open as child windows.
- Dockable/floating tool windows, each independently closable.
- Live per-interface traffic graphs, updating ~1 Hz without interaction.
- Torch: live per-host/per-connection bandwidth attribution.
- Staged apply: WinBox 4 applies immediately but the router's commit semantics
  we mirror are UCI's `changes`/`commit`/`revert` — we expose them as a visible
  changes list, which WinBox itself lacks.
- Security model: ECSRP + AES128 in WinBox; ours is rpcd session tokens over HTTPS
  (see ubus-rpcd-read-surface.md).

## Distribution model to copy

- Single static binary per platform from one codebase, signed, tiny asset dir.
