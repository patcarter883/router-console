# ubus/rpcd read-surface inventory

The client's protocol layer. Every view maps to calls on this surface; nothing else.
All calls go over HTTPS via uhttpd's `/ubus` endpoint (uhttpd-mod-ubus, default on
Luci installs) as JSON-RPC, with rpcd session tokens for auth. Event push is
available via `/ubus/subscribe/` (uhttpd advisory CVE-2025-53386 names it; it is
shipped in 25.12.x) — WebSocket for graphs at >=1 Hz; poll JSON-RPC at 1 Hz is the
fallback and is fine for a first pass.

## Router auto-discovery (connect window)

WinBox discovers routers with MNDP; the OpenWrt equivalent is `umdns` (mDNS,
UDP multicast on the LAN, RFC 6762). Mechanism: the client joins 224.0.0.251,
parses the responder records and lists routers in the connect window with
hostname + IP, one click to prefill the connect form.

### Measured records (umdns 2026.10.04~defe7ccf-r1 on 25.12.6, 2026-11-12)

Captured on the lab segment with the guest's umdns enabled — not from memory.
`scripts/mdns-probe.py --listen` decodes them; `scripts/mdns-probe.py <name>:<type>`
sends a query:

| record | value | TTL |
| --- | --- | --- |
| `openwrt-dev.local` A | `192.168.78.1` | 4500 |
| `openwrt-dev.local` AAAA | `fe80::5054:ff:fe12:3458` (link-local) | 4500 |
| `_services._dns-sd._udp.local` PTR | `_ssh._tcp.local` | 4500 |
| `_ssh._tcp.local` PTR | `ssh_22._ssh._tcp.local` (from `ubus call umdns announcements`) | — |
| `ssh_22._ssh._tcp.local` SRV | port 22, target `openwrt-dev.local` | — |
| `ssh_22._ssh._tcp.local` TXT | `daemon=dropbear` | — |

The hostname is the A record's first label (`openwrt-dev`), and the IP is the
address on the segment the client is listening on. The service set comes from
dropbear registering `_ssh._tcp` with umdns; a router with more daemons
announcing will show more PTRs.

### umdns is announce-only (measured)

- It sends its records **once at start-up** (probe + unsolicited response, then
  it is silent). 70 s and 4-minute captures on the lab segment after a restart
  showed no further announcements.
- It **does not answer queries**: `_services._dns-sd._udp.local` PTR,
  `_ssh._tcp.local` PTR/ANY, `openwrt-dev.local` A/ANY, QM and QU classes, from
  port 5353 and from an ephemeral port, unicast to `192.168.78.1:5353`, with
  `jail` on and off — the queries arrive at the guest (tcpdump on its `eth2`)
  and nothing comes back.
- `ubus call umdns update|reload`, `set_config`, SIGHUP and SIGUSR1 do not
  re-announce either.

Consequence: a client that starts more than a few seconds after the router
booted would never see it. The dev VM therefore runs
`/etc/init.d/umdns-announce` (installed by `scripts/vm-discovery-setup.sh`),
which restarts umdns every 5 s so the announcements recur — measured at 5 s
intervals. That is a router-side deployment decision, not client magic: the
client stays a plain mDNS listener and a managed router fleet should either run
the same timer or a responder that answers queries.

### Client

`src/protocol/MdnsDiscovery.cpp` (worker thread, `QUdpSocket`):

- binds `0.0.0.0:5353` with `ShareAddress|ReuseAddressHint` (avahi holds the
  port on developer desktops) and joins 224.0.0.251 per interface;
- parses A (hostname + IPv4), PTR (service enumeration) and SRV (port, service)
  records, attributes a service type to the responder's address, and skips
  addresses that belong to the client itself — avahi on the client announces
  its own name on every segment, including the lab one;
- sends a `_services._dns-sd._udp.local` browse query every 5 s for responders
  that do answer, so discovery also works where umdns's silence is replaced by
  a query-capable responder;
- feeds `DiscoveredHostsModel`, exposed to QML as `AppEngine.discoveredHosts`
  with `hostname`/`address`/`port`/`target`/`services` roles. Routers rank
  first (dropbear's `_ssh._tcp`, demoted for consumer mDNS service types), the
  list is capped in the dialog and scrolling.

Discovery must degrade silently: no umdns, no multicast or no interface =>
empty list, manual address entry (the fields below the list) always works.
Connecting never waits for discovery.

Measured: `DISCOVERED openwrt-dev 192.168.78.1:443 after 808 ms` on a warm
segment and 3.4 s on a clean start after boot (see docs/dev-vm-recipe.md).


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
