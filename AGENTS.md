# Agent brief — router-console

Read PROJECT.md and docs/ first. The finish line is the north star
`router-console` (command to re-read it is in PROJECT.md); the goal prompt from
the star outranks anything you infer.

## Hard rules

1. Dev target is the local OpenWrt x86 VM (docs/dev-vm-recipe.md). Never develop
   against or configure the live routers (video router 192.168.1.1 especially:
   PTP GM + chrony, UDP 319/320 are sacred).
2. C++20, Qt 6, QML for views, CMake >= 3.24. No other GUI frameworks, no
   webviews, no embedding LuCI pages.
3. Protocol layer is ONLY the ubus/rpcd surface in docs/ubus-rpcd-read-surface.md.
   If a view needs data not on that surface, extend the surface docs first, then
   the code. JSON-RPC structs in typed C++ (no nlohmann json::json leaking past
   the protocol layer boundary).
4. Structured output discipline: any LLM/agent plumbing in the client (none
   expected) uses model-level structured output, not prompt-only JSON.
5. Comments: minimal, describe current state, no change logs (commit messages
   carry history).
6. Cross-platform from the first commit: no glibc-only calls in app code, no
   platform ifdefs without a CI build covering both. MSVC or MinGW toolchain
   decision recorded in docs/ when made.
7. Thread-safe UI: all network I/O off the UI thread; QML receives state via
   models/properties, never direct widget pokes from worker threads.
8. The dev VM is a single shared instance whose disk lives in the MAIN checkout's
   `.vm/` (untracked). Never copy it into a worktree and never `git worktree
   remove` a checkout while its `.vm` is in use: qemu keeps running on the
   unlinked inode and the state dies with it (this happened once). To run a
   second VM, use FWD_* port overrides in a copy created explicitly for that
   run, and merge nothing back but code.
   Also: ALWAYS run the suite as `ctest -LE vm-boot`. The `discovery-after-boot`
   test (label vm-boot) KILLS every qemu on the host and daemonizes its own
   replacement — running it inside a bare `ctest` destroys the session VM
   mid-suite and leaves an orphan that dies later. Run the vm-boot label only
   in isolation, when you intend it to take VM ownership.
9. Verify before claiming: run the build, run the smoke curl against the VM,
   paste real output into the north-star evidence file:

       python3 ~/.hermes/skills/software-development/north-star/scripts/north_star.py evidence router-console --add "..."

## Building

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build

Qt6 must be present (Arch: qt6-base; the client targets dynamic Qt on dev
boxes — the static single-binary distribution trick is a release concern, see
docs/winbox4-reference.md, not a build-time requirement).

## First milestones (star order)

1. Connect window + ubus login against the VM (token round-trip).
2. Interface list + 1 Hz graphs (network.device.status polling).
3. UCI staged editor for one domain (sqm), then the rest.
4. Tool windows.
5. Plugin mechanism + the three custom app views.
6. Windows build in CI.
