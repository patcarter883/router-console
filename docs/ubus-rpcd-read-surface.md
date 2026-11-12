# ubus/rpcd read-surface inventory

The client's protocol layer. Every view maps to calls on this surface; nothing else.
All calls go over HTTPS via uhttpd's `/ubus` endpoint (uhttpd-mod-ubus, default on
Luci installs) as JSON-RPC, with rpcd session tokens for auth. Event push is
available via `/ubus/subscribe/` (uhttpd advisory CVE-2025-53386 names it; it is
shipped in 25.12.x) — WebSocket for graphs at >=1 Hz; poll JSON-RPC at 1 Hz is the
fallback and is fine for a first pass.

## Router auto-discovery (connect window)

WinBox discovers routers with MNDP; the OpenWrt equivalent is `umdns` (mDNS-based,
UDP multicast on the LAN). Mechanism: client joins the multicast group and listens
for OpenWrt service announcements; discovered routers appear in the connect window
with hostname + IP, one click to prefill.

- Do NOT hardcode service record names from memory: bring up the VM with umdns
  enabled, capture the actual announcements (`tcpdump -i br-lan udp port 5353`),
  and write the record names/TXT fields observed into this doc. Then implement
  the listener against the measured records.
- Discovery must degrade silently: no umdns => manual address entry (already the
  fallback in Connect.qml). Never block connect on discovery.

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

- rpcd login: POST /ubus {"jsonrpc":"2.0","id":1,"method":"call","params":["000...","session","login",
  {"username","password"}]} -> ubus_rpc_session token + sessionid; every later
  call carries the token. ACLs via /usr/share/rpcd/acl.d — a client ACL file
  ships in our custom package feed granting exactly the surfaces above.

Measured against 25.12.6 (see docs/dev-vm-recipe.md for the full smoke test):

    {"jsonrpc":"2.0","id":1,"result":[0,{"ubus_rpc_session":"<32 hex>",
      "timeout":300,"expires":300,"acls":{"access-group":{...},"ubus":{...},"uci":{...}},
      "data":{"username":"root"}}]}

- `result[0]` is the ubus status code (`0` OK, `6` PERMISSION_DENIED, `5` NO_DATA,
  `4` NOT_FOUND, `3` METHOD_NOT_FOUND, `1` INVALID_COMMAND, `2` INVALID_ARGUMENT,
  `7` TIMEOUT, `8` NOT_SUPPORTED). A rejected call may return a **one-element**
  array — a wrong password answers exactly `{"result":[6]}`, with no data object
  — so parsers must not assume `result[1]` exists. A uhttpd-level rejection
  instead carries `{"error":{"code":-32000,"message":"..."}}`, typically with
  HTTP 403.
- `timeout` is 300 s from the default rpcd ACL session timeout: the client must
  re-login on status 6 (or proactively before `expires`) and retry the call once,
  not surface an auth failure to the user.
- The token is per-session and unprivileged-by-IP: rpcd ENFORCES the `acls` map it
  returns, so a call absent from that map fails with status 6 even as root.
- TLS: uhttpd serves a self-signed certificate generated on first start. The
  client pins the peer certificate's SHA-256 fingerprint per host, stores it on
  first trust (explicit user confirmation in the connect window), and refuses a
  changed fingerprint. Blanket `ignoreSslErrors` is not acceptable.
- `luci-rpc` in this doc is the **ubus object** (`luci-rpc.getBoardJSON`,
  `getNetworkDevices`, ...). In 25.12 it ships inside the `luci-base` package —
  there is no package named `luci-rpc` any more.
