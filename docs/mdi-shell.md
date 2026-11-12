# MDI shell: one parent frame, many live windows

WinBox opens every view as a child window inside one desktop; the left tree stays
visible and several views run at once. This is how router-console does the same in
Qt Quick, and why this shape rather than the alternative.

## The choice (measured 2026-11-13, X11 software renderer on :101)

Qt offers two honest equivalents: N OS-level `QQuickWindow`s over one engine, or
one `ApplicationWindow` that hosts draggable frame items. This client uses the
second — one shell window, an MDI desktop on its right-hand side.

| | N OS windows | one frame (this shell) |
| --- | --- | --- |
| top-level X windows, 2 views open | 3 (shell + Interface br-lan + Log) | 1 |
| top-level X windows, 4 views open | 4 | 1 |
| RSS, 4 views open | 108,504 kB (2 views) | 106,816 kB (4 views) |
| z-order / geometry owner | the window manager | the shell |

Why the single frame:

- **One engine, one model set.** `AppEngine`, `InterfacesModel` and every tool are
  singletons of one QML engine; each view's `Component.onCompleted` refresh is
  already a fetch. Separate windows would still share those objects, but every
  new window would repeat the fetch chain and the 1 Hz polling would multiply
  per open window.
- **The tree is the launcher.** With OS windows the tree can be covered by the
  windows it opened; in the frame the tree is a fixed column that frames never
  cross.
- **One render surface.** Graph repaint cost stays flat as windows are added:
  RSS moved by ~1.7 MB for four views versus two under the old model.

The cost of the choice is stated in *Limits* below.

## Structure

| file | role |
| --- | --- |
| `src/qml/MdiWindowFrame.qml` | one frame: title bar (drag, close), body, resize grip, `raise()`, geometry persistence, `releaseResources()` hand-off |
| `src/qml/Main.qml` | the manager: `openWindow`, `findWindow`, `dropWindow`, `closeAllWindows`, `cascadeWindows`, `tileWindows`, `clampWindows`, plus one opener per view kind |
| `src/ui/MdiGeometry.{h,cpp}` | cascade/tile/clamp math and the `QSettings` store; pure functions, unit-tested (Core-only, no GUI) |
| `src/ui/MdiRaiseFilter.{h,cpp}` | click-to-front: a view's own mouse areas and item views consume presses before any ancestor handler, so the shell window filters `MouseButtonPress`, finds the top-most frame under the point and calls its QML `raise()`, then lets the event continue untouched |

Views are unchanged in content: each `*Window.qml` is rooted on a `Rectangle` and
exposes `windowTitle`, `minimumWidth`, `minimumHeight`. The frame drops the view
into its body and drives size; the view keeps every behaviour it had.

## Behaviour

- **Many at once.** Opening a view never closes another, and reopening the same
  view raises the frame already there instead of stacking duplicates.
- **Z-order.** Pressing anywhere in a frame — title bar or body — raises it and
  marks its title bar active.
- **Cascade / Tile / Close all** in the toolbar. Cascade uses 62 % × 66 % of the
  desktop with a 28 px step across 8 slots; tile is a `ceil(sqrt(n))` grid with a
  3 px gap.
- **Drag and resize.** Title bar drags the frame (clamped so the bar stays
  reachable); the bottom-right corner resizes it, never below the view's own
  minimum size.
- **Geometry survives restarts.** Each frame persists its rect under
  `mdi/<key>/geometry` in `QSettings`, keyed by a stable window key —
  `interface/br-lan`, `routing/routes`, `tool/log`, `config/sqm`, `plugin/<id>`.
  On open the saved rect is restored and clamped into the current desktop, so a
  window remembered from a larger screen still shows its title bar.
- **Close per window** (✕) and Close all. A view with a `releaseResources()` hook
  gets it called on close: the config editor drops its reference, a plugin
  session stops polling.
- **Disconnect closes everything**, because every view is reading a session that
  no longer exists. Single-router-per-instance is unchanged.

## Limits (deliberate)

- One resize grip, the bottom-right corner; edges are not draggable.
- No maximize/restore, no docking to edges, no tab groups.
- Frames live inside the application window: they cannot be moved to a second
  monitor on their own (move the application window instead).

## Automation hooks

Used by the evidence scripts and by CI, all repeatable:

    --open-interface br-lan --open-interface lan
    --open-routing routes --open-routing ospf
    --open-tool log --open-tool terminal
    --open-config sqm --open-plugin modemaim
    --mdi-layout tile|cascade      # arrange what the flags opened

`scripts/m15-shot.sh multi|tile|restored` captures the shell on a headless X
server (`:101`), and `tests/m15_mdi_structure.sh` fails the suite if a view ever
goes back to being a top-level window.
