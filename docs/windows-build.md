# Windows build

Milestone 7 requirement: one codebase, builds on Linux and Windows.

## Toolchain decision

MSVC (Visual Studio 2022, x64) with the Qt 6.8.3 MSVC 2022 package. Recorded
here because `AGENTS.md` requires the MSVC-vs-MinGW decision to live in `docs/`.

Why MSVC: it is the toolchain the Qt Windows binaries are built with, so no ABI
translation layer, and it is what `windows-latest` ships. MinGW would need a
separate Qt build and a cross-ABI check for any dependency later.

Qt itself comes from aqtinstall, which fetches the official Qt archives - the
same packages the online installer serves. No Qt account, no `choco install`
of a GUI installer that cannot run headless.

## CI

`.github/workflows/ci.yml` runs a two-leg matrix (`ubuntu-latest` + GCC with
Ninja, `windows-latest` + MSVC with Ninja) on Qt 6.8.3, builds `router-console`, runs `ctest -LE vm-boot`, and uploads
`router-console-linux-x86_64.tar.gz` and `router-console-windows-x86_64.zip`.

`ctest` skips the integration tests with exit code 3 when the dev VM is not
reachable (that is the `SKIP_RETURN_CODE`), so a CI run proves compilation,
linking and the non-VM test surface, not router behaviour.

The Windows leg enters the MSVC environment with `ilammy/msvc-dev-cmd` instead of
using a Visual Studio generator: `windows-latest` now ships Visual Studio 18, and
CMake's `Visual Studio 17 2022` generator fails to find an instance there. vcvars
plus Ninja has no such dependency on a specific VS version.

The `vm-boot` labelled tests (`discovery-after-boot`, `uci-sqm-staged-reboot`)
reboot the VM and are excluded from CI.

## Runtime libraries

The Windows artifact is deployed with `windeployqt` before upload, so the zip
contains `router-console.exe` plus the Qt DLLs and the QML/Quick plugins it
needs. Unzip it anywhere and run the exe: no Qt install required on the target
box.

The Linux artifact is the bare binary. It links the Qt 6.8 shared libraries
(soname-compatible with newer Qt 6.x), so run it on a desktop with Qt 6
installed - the CachyOS box builds locally anyway.

## Platform ifdefs

None. The milestone added no `#ifdef` and no platform-specific call: the only
OS-facing code is Qt (QTcpSocket, QUdpSocket, QNetworkInterface, QProcess,
QSettings), which already abstracts both. If a future change needs one, it
needs a CI leg that compiles it - both legs above are always green before a
merge.

## Manual verification (owner hand-back)

CI builds and links the Windows binary but cannot launch a GUI, so the last
step belongs to the owner on a real Windows box:

1. Download `router-console-windows-x86_64.zip` from the green `ci` run
   (Actions tab -> run -> Artifacts).
2. Unzip, run `router-console.exe`. The connect window must appear and the
   mDNS list must populate if a router is on the same segment.
3. Connect to a router (or the dev VM's forwarded port) and confirm the
   interface list and the 1 Hz graphs update.
4. Record the result in the north star:

       python3 ~/.hermes/skills/software-development/north-star/scripts/north_star.py \
           evidence router-console --add "windows: router-console.exe from CI artifact <run id> launched, connected to <router>, graphs live"
