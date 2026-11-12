# ubus/rpcd read-surface inventory

The client's protocol layer. Every view maps to calls on this surface; nothing else.
All calls go over HTTPS via uhttpd's `/ubus` endpoint (uhttpd-mod-ubus, default on
Luci installs) as JSON-RPC, with rpcd session tokens for auth. Event push is
available via `/ubus/subscribe/` (uhttpd advisory CVE-2025-53386 names it; it is
shipped in 25.12.x) — WebSocket for graphs at >=1 Hz; poll JSON-RPC at 1 Hz is the
fallback and is fine for a first pass.

## System / board

- `system.board` — model, firmware (release), kernel, hostname.
- `system.info` — uptime, load.
- `system.cpu` — aggregate stats.
- rpcd `counters` (netjson / rpcd) or `/proc/stat` via `file.exec` — never needed; use ubus.

## Interfaces (the graphs window)

- `network.interface.dump` — all interfaces + up/uptime/l3-device.
- `network.device.status` — per-device: stats (rx/tx bytes, packets, errors),
  type, present. Poll delta at >=1 Hz => traffic graphs.
- `luci-rpc.getNetworkDevices` / `getWirelessDevices` (luci-rpc package) — the
  curated device lists LuCI itself uses; prefer these for tree building.
- `luci-rpc.getBoardJSON`, `getDSLStatus`, etc. as needed.

## Config read/write (all core domains)

- `uci` (ubus object): `get`, `get_all`, `set`, `add`, `delete`, `apply`,
  `confirm`, `revert`, `changes`. THE staged-apply primitive:
  read -> edit (staged) -> `changes` (list) -> `apply`+`confirm` or `revert`.
  Domains map to UCI packages: system, network, wireless, firewall (fw4),
  dhcp, and sqm (sqm-scripts stores its config in UCI too).
- `luci-rpc.getInitList` etc. for service state.

## Tool tier

- ping/traceroute: OpenWrt ships `ping` and `traceroute` binaries; drive via
  rpcd `file.exec` (root) or wrap output — better: ubus `luci` nettools if
  present; otherwise exec and stream stdout lines.
- torch-style per-host view: conntrack read (`luci-rpc` / `/proc/net/nf_conntrack`
  via file.read) sampled at ~1 Hz, keyed by src/dst with byte counters where
  available; per-interface device stats give the rate check. Caffeine: no
  per-flow counters in stock conntrack without nftables flowtable accounting —
  first pass: per-host from conntrack tuple deltas + interface totals.
- files + sysupgrade: `file.list/read/write/remove` (rpcd), `luci-rpc.getBackup`,
  and `rpc-sys` object (`upgrade_start`, `upgrade_test`, `upgrade_keep`) which is
  exactly what LuCI's flash page uses. Package install: rpc-sys + apk wrapper.
- SQM/CAKE stats: sqm-scripts has no ubus surface; read `tc -s qdisc show`
  output via rpcd file/exec (`/usr/libexec/sqm` helpers run as root) and parse.
  cake_mq-aware: iterate all cake instances when backported kernels land.

## Custom app surface (the plugin contract)

Each custom LuCI app exposes its data one of two ways; the client plugin
declares which:
1. A ubus namespace (e.g. rist2rist's rpcd script — verify against the repo at
   open-broadcast/open-broadcast-product-codec/luci-app-rist2rist/).
2. An HTTP JSON endpoint under /luci-static/ (modemaim serves towers.json this
   way; its views fetch() it — documented in ~/workspace/cellular/PROJECT.md).

## Auth

- rpcd login: POST /ubus {"method":"call","params":["000...","session","login",
  {"username","password"}]} -> ubus_rpc_session token + sessionid; every later
  call carries the token. ACLs via /usr/share/rpcd/acl.d — a client ACL file
  ships in our custom package feed granting exactly the surfaces above.
