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

| plugin | surface | state |
| --- | --- | --- |
| `rist2rist` | procd `service.list {name:rist2rist}` + `uci get rist2rist` | real surface; the LuCI app has no rpcd object of its own |
| `modemaim` | `modemaim status`, `modemaim origin` + HTTP `/luci-static/resources/modemaim/towers.json` | real rpcd surface and real static-file pattern |
| `nettime` | `nettime status` + procd `service.list` | status method is a **mock**; see below |

### rist2rist

The vendored app (`open-broadcast-product-codec/luci-app-rist2rist`) grants only
`service list`, `uci rist2rist` and `logread -e rist2rist`; its LuCI view reads
exactly those. The plugin binds to the first two. On the VM the package is not
built: a `sleep` daemon registered by `/etc/init.d/rist2rist` and
`/etc/config/rist2rist` (main + two destinations) give `service.list` and `uci`
the real shapes to answer with. The bridge's own RIST metrics are not exposed on
the ubus surface, so the view does not show them — it shows daemon state and
configuration, which is what the LuCI page shows too.

### modemaim

`ubus call modemaim status` answers signal, serving cell, neighbours and the
mapped site; the tower extract is deliberately **not** served over ubus (over
1 MiB it unregisters the whole rpcd object). The view therefore pulls
`towers.json` over HTTP and computes distance/bearing from `modemaim origin`,
mirroring the LuCI views. On the VM, `scripts/vm-mock-apps.sh` installs a mock
rpcd script with the real script's JSON keys (`cellular/luci-app-modemaim/root/usr/libexec/rpcd/modemaim`)
and a small extract with the real schema (`site, lat, lon, band, mhz, bw, eirp,
h, azs, hbw, carrier` + `data_date`). No modem is present, so the values are
fixed; the shape is what the client binds to.

### nettime

The app is a GstNetTime provider on UDP 5637 on the video router. It exposes no
rpcd object: its real surface is the clock itself and procd's view of the daemon.
The client does not implement the GstNetTime wire protocol — the view reads a
`nettime status` method and the procd service state, and says so in the
window. That status method is a **mock** on the VM (port 5637, `CLOCK_REALTIME`,
uptime from `/proc/uptime`, client count). Adding a real status method to the
package (serving the provider's own counters) is the follow-up for the video
router; nothing in the client has to change when it lands, only the fixture goes
away.

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
| `plugin-loading` | the four manifests parse; each app declares the sources it binds to; a missing view / missing sources is rejected with its reason |
| `plugin-stub` | a stub added through the interface alone appears in the tree, with `git status` clean outside `plugins/` |
| `plugin-data-rist2rist` | session receives procd service state and the `rist2rist` UCI package (main + destinations) |
| `plugin-data-modemaim` | session receives `modemaim.status`, `modemaim.origin` and the tower extract over pinned TLS |
| `plugin-data-nettime` | session receives `nettime.status` (port 5637, CLOCK_REALTIME) and the procd service state |

The data tests need the VM fixtures: `scripts/vm-mock-apps.sh` installs them and
prints each surface's raw answer.

Screenshots: `scripts/plugin-gui.sh` (headless X, `PLUGIN=<id>`; `PLUGIN=tree`
captures the tree).
