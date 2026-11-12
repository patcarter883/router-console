# Plugin mechanism

A plugin is a directory under `plugins/` holding `plugin.json` and the QML view it
names. The client scans that directory at start-up (`Plugin::loadPlugins`), so a
new custom app view is a file drop: no CMake entry, no QML-module entry, no core
source change. `tests/plugin-stub.sh` asserts exactly that (requirement 6).

## Manifest

```json
{
  "id": "rist2rist",
  "title": "RIST Proxy",
  "order": 60,
  "interval": 2000,
  "view": "Rist2Rist.qml",
  "sources": [
    { "id": "service", "transport": "ubus",
      "object": "service", "method": "list", "args": { "name": "rist2rist" } },
    { "id": "config", "transport": "ubus",
      "object": "uci", "method": "get", "args": { "config": "rist2rist" } }
  ]
}
```

| field | meaning |
| --- | --- |
| `id` | lowercase, hyphenated; unique; used as the tree key |
| `title` | tree label and window title |
| `order` | tree position (lower first) |
| `interval` | default poll period in ms (floor 250) |
| `view` | QML file relative to the plugin directory; loaded as a plain file URL, not through the app's QML module |
| `sources[]` | the data the view needs |

A source is one of:

- `ubus` — `object` + `method` + `args`, called with the session the client already
  holds. `service.list`'s answer for a stopped service is a one-element result
  array; the client turns that into an empty payload, so a view sees "not running"
  rather than an error.
- `http` — `path` fetched from the connected router over the same pinned TLS
  connection, e.g. modemaim's static `/luci-static/resources/modemaim/towers.json`.
  An unpinned certificate is a failure, never an ignore.

Per-source `interval` overrides the plugin `interval` (modemaim polls status at
3 s but the ACMA extract at 60 s).

Rejected manifests stay in the tree with the reason shown
(`"Net Time Provider — view 'X.qml' does not exist"`), so a broken plugin is
visible instead of silently missing.

## View contract

The window loads the plugin QML with a `plugin` property:

```qml
Item {
    property var plugin: null
    readonly property var status: plugin.samples.status.fields
}
```

`plugin` is a `Plugin::Session` and exposes, per declared source, the last
sample: `samples.<id>` = `{ ok, error, at, fields }`. `fields` is the app's own
JSON object. `plugin.host`, `plugin.revision`, `plugin.refresh()` complete the
surface.

## Threading

Nothing a plugin does runs on the UI thread except the timer and the mapping of
already-received JSON into the samples map:

- ubus calls are queued to `UbusClient`, which lives in its own thread;
- HTTP fetches run in a dedicated `plugin-http` thread with its own
  `QNetworkAccessManager`, and the JSON is parsed there;
- results arrive back as queued signals; requests that do not answer in 15 s
  become a failed sample instead of a stuck spinner.

## Typed boundary

`Plugin::Spec` / `DataSource` (manifest), `Plugin::Sample` (one source's state)
and `Ubus::Error` are typed C++ structs. Only `Sample::fields` is a
`QVariantMap`: the payload belongs to the app, and a declarative plugin cannot
have a compiled-in schema. Views read it as `samples.<id>.fields`.

## The three app plugins

Each row names what is now **real** on the dev VM — installed from the app's own
package or deploy path, verified by its data test — and what remains thinner
than the app on the live router.

| plugin | bound surface | on the VM | real? |
| --- | --- | --- | --- |
| `rist2rist` | procd `service.list {name:rist2rist}` + `uci get rist2rist` | `luci-app-rist2rist` 1.0.0-r49 apk; `/usr/bin/rist2rist` running on UDP 5000 with the packaged init, config, rpcd object and ACL | **real package and real daemon** |
| `modemaim` | `modemaim status`, `modemaim origin` + HTTP `/luci-static/resources/modemaim/towers.json` | the app's own rpcd plugin, ACL and the real ACMA extract (3,810 rows), deployed by `cellular/router/deploy-modemaim.sh` | **real rpcd surface and real extract**; no modem, so signal is absent (the m17 fixture stands one in) |
| `modemaim-map` | `modemaim status/origin` + towers JSON | as `modemaim` | **real rpcd surface and real extract**; the map needs no modem |
| `modemaim-aim` | `modemaim signal/status/origin` + towers JSON | as `modemaim`; metrics need `scripts/vm-aim-fixtures.sh` (below) | **real rpcd surface, real extract, fixture below the mbimcli boundary** |
| `nettime` | status object + procd `service.list`, both under the router-side object name (`scripts/nettime-routerside.sh`) | the video project's nettime apk; its daemon serves `CLOCK_REALTIME` on UDP 5637 | **real provider daemon**; the status object is a shim (below) |

### rist2rist

The packaged app grants `service list`, `uci rist2rist` and
`logread -e rist2rist` to a LuCI session, and its own view's status widget reads
exactly `service.list` (running flag + pid) plus the log. The plugin binds the
first two, so the view and the LuCI page show the same figures from the same
call.

The package also ships an rpcd object (`rist2rist status/get_config/wan_status/
set_config/...`), which is **not** bound: every read method on it verifies the
bridge's pair token, a per-bridge secret that has no place in a plugin manifest.
On the VM an unclaimed bridge answers:

    # ubus call rist2rist status
    {"ok": false, "error": "unclaimed", "message": "this bridge has not been claimed; call claim first"}

The view therefore shows what the packaged config holds — listen URL, profiles,
buffer/RTT/reorder window, config-authority state and the destination list —
plus procd's live pid and the daemon's real argv. The RIST transport counters
themselves are not on any token-free ubus surface, so they are not shown; that is
a property of the app, not a gap in the client.

### modemaim

`ubus call modemaim status` answers signal, serving cell, neighbours and the
mapped site; the tower extract is deliberately **not** served over ubus (over
1 MiB it unregisters the whole rpcd object). The view pulls `towers.json` over
HTTP and computes distance/bearing from `modemaim origin`, mirroring the LuCI
views.

The m6 window never actually rendered — its evidence screenshot showed
`Plugin view failed to load`, because the list header set `rightPadding`, which
`RowLayout` does not have (Qt rejects the whole component). The header now binds
the view's own width and the padding properties are gone; all four plugin QML
files load through `QQmlComponent`.

The VM runs the real plugin (`cellular/luci-app-modemaim/root/usr/libexec/rpcd/modemaim`,
627 lines) against the real extract (`cellular/data/towers.json`: 3,810 ACMA
rows, `data_date 2026-11-07T09:06:09Z`). No MBIM modem exists on the VM, so
`mbimcli` is absent and the honest answer is an empty measurement:

    "state": "unknown", "rsrp": "", "serving": {"pci": "", ...}, "neighbors": []

`scripts/vm-real-apps.sh` sets a fixed origin inside the extract's bounding box
through the app's own `modemaim setorigin` method — a bench VM has no GPS, and
without an origin the view cannot compute distance or bearing. The view renders
the nearest licensed sites from the real extract at that origin.

### modemaim map and aim (m17)

`docs/aim-view.md` carries the design and the measured surface. In short: the
extract's 3,810 rows aggregate to 328 sites with band, MHz, licensed azimuths
and beamwidth; the map draws them on a local equirectangular grid centred on
`modemaim origin`, with click-through detail; the aim window plots the same
sites on a north-up bearing rose, holds a running peak per metric from the
500 ms `signal` lane, and stores the marks the operator drops while turning the
mast by hand. The serving cell is correlated through `status.mapped.site` when
the eNB map has the cell, and the views say "unmapped" when it does not.

There is no heading inference: the owner's correction is that a timed sweep
cannot work on a hand-rotated mast, so bearings are operator-entered and the
peak hold is a plain running maximum. Marks and peaks are session memory only —
no disk persistence, same scope as the m16 history.

The VM has no MBIM modem, so the aim numbers come from
`scripts/vm-aim-fixtures.sh`: a stand-in `/usr/bin/mbimcli` with metrics that
swing over a minute, plus the `enb-acma.csv` row mapping its serving cell to
site 10021260 in the real extract. Everything from rpcd down is the app's own
code; `plugin-data-modemaim` therefore now sees a modem-present answer on a
fixture VM.

### nettime

The app is a GstNetTime provider on UDP 5637: on the VM the packaged daemon runs
and logs its package name followed by `serving GstSystemClock on 0.0.0.0:5637`.
It exposes no rpcd object — its only surfaces are the UDP clock and procd's view
of the daemon. The client does not implement the GstNetTime wire protocol, so
the view reads a status object plus the procd service state and says so in the
window.

The client's plugin is `nettime`. The apk and the procd service it reads are the
package's own, published under a name this repository keeps in exactly one place
— `scripts/nettime-routerside.sh` — because nothing else needs to spell it. The
status object (`nettime`) is this client's shim, not the package's.

That status object is **ours, not the app's** (`scripts/vm-fixtures/rpcd/nettime-shim`),
and it is measured, not invented: `pid` is procd's instance pid, `port` is the
UDP port that pid actually has bound (`netstat -lnup`), `enabled` is
`/etc/init.d/<router-side name> enabled`, and `clock` is `CLOCK_REALTIME` because
the package's own source forces it. `uptime` and `clients` are **absent**: neither
procd nor the provider records a start time or a client count, and the data test
asserts they stay absent, so the view shows the pid it can prove instead of two
permanently empty cells. Adding a status method to
the package is the follow-up for the video router; nothing in the client changes
when it lands — only the shim goes away.

## Building and installing the real apps

`scripts/vm-real-apps.sh` installs all three and prints each surface's raw
answer. `scripts/vm-mock-apps.sh` still installs the milestone-6 stand-ins and is
untouched; to go back, `apk del luci-app-rist2rist "$NETTIME_ROUTER_OBJECT"` (as
exported by `scripts/nettime-routerside.sh`), then run it.

The two packages that have a Makefile are built for x86/64 in the released SDK
for **25.12.6** (`openwrt-sdk-25.12.6-x86-64_gcc-14.3.0_musl`, the exact release
the VM runs):

    cd openwrt-sdk-25.12.6-.../
    ./scripts/feeds update base packages
    ./scripts/feeds install libopenssl cjson
    env -u LD_PRELOAD TOPDIR=$PWD make -C package/luci-app-rist2rist compile

That build runs meson + ninja over the package's own `src/` (librist and
`tools/rist2rist`), so the apk's binary is the app's, not a stand-in.

`nettime` has **not** been rebuilt in the 25.12.6 SDK: it needs gstreamer
staged, and this host's SDK build of `gettext-full` (a glib2 dependency) fails, so
the apk installed on the VM is the 1.0-r1 build made from the same unchanged
source in the 25.12.4 SDK on 2026-10-19, kept at
`~/code/openwrt/artifacts/` under the package's published name (see
`scripts/nettime-routerside.sh`). What matters for requirement 5
is verified either way: the packaged daemon runs and serves UDP 5637. Rebuilding
it is a follow-up on a host whose SDK can stage gstreamer.

`luci-app-modemaim` ships no Makefile — the cellular project installs it from
`cellular/router/deploy-modemaim.sh`, which is what the VM script runs (with a
`PATH` shim so the script's bare `ssh` reaches the forwarded port, and with
`/usr/share/modemaim` created first: the script writes into it before creating
it). Two apk quirks the script handles: the VM's dropbear has no `sftp-server`,
so files go over `ssh 'cat >'`, and `apk` keeps its copy of a conffile that
already exists untracked as `<file>.apk-new`, so those are adopted — otherwise
the mock's init and config would keep winning.

Build host notes: `make package/<name>/compile` at the top level of a fresh SDK
drags in the target's device kmods; a direct `TOPDIR=$SDK make -C package/...`
build avoids that, and needs `LD_PRELOAD` cleared for the host cmake wrapper.
## Adding a plugin

```
plugins/my-app/
  plugin.json
  MyApp.qml
```

Nothing else. `ctest -R plugin-loading` re-reads the tree; `tests/plugin-stub.sh`
proves a generated stub loads from a bare plugin root and that no core file
changed.

## Tests

| test | what it proves |
| --- | --- |
| `plugin-loading` | every manifest parses; each app declares the sources it binds to; a missing view / missing sources is rejected with its reason |
| `plugin-stub` | a stub added through the interface alone appears in the tree, with `git status` clean outside `plugins/` |
| `plugin-data-rist2rist` | procd reports a **running** instance whose command is `/usr/bin/rist2rist`, with its pid and full argv; the `rist2rist` UCI package carries the packaged schema (`managed`, `api_port`) and a destination |
| `plugin-data-modemaim` | `modemaim.status` carries the real key set and, with no modem, an empty measurement (`state: unknown`, empty `serving`, no neighbours) rather than filled-in values; `modemaim.origin` answers the configured origin; the real 3,810-row extract arrives over pinned TLS |
| `m17-aim-model` | bearing/distance for known pairs, site aggregation from per-band rows, peak-hold over synthetic series, mark round-trip, ingest dedupe (no router) |
| `m17-aim-live` | 32 s on the `signal` lane: ≥ 200 sites with positions and bearings, peak-hold equal to the best figures of the run, serving cell mapped to an ACMA site, a mark added through the model API that reads back |
| `m17-aim-ui` | both m17 plugin windows load headless with no QML errors and write the evidence shots (skips without a VM) |
| `plugin-data-nettime` | the status shim's `pid` **equals procd's** pid for the real provider, port 5637, `CLOCK_REALTIME`, enabled; `uptime`/`clients` are absent because the app provides neither |

The data tests now run against the **real app packages**: `scripts/vm-real-apps.sh`
installs them and prints each surface's raw answer. A mock-only VM fails
`plugin-data-rist2rist` at `daemon command = /bin/sleep 86400` — deliberately,
because the packaged daemon is the surface requirement 5 binds. The mock script
is still the right tool for UI work (`scripts/vm-mock-apps.sh` + `scripts/plugin-gui.sh`).

Screenshots: `scripts/plugin-gui.sh` (headless X, `PLUGIN=<id>`; `PLUGIN=tree`
captures the tree); `scripts/m17-shot.sh` for the map and aim windows, which also
types a bearing and presses Enter so the aim shot holds a real mark.
