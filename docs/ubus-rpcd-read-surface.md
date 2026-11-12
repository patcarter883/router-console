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

Measured against 25.12.6 (`ubus call network.interface dump`, `ubus call
network.device status`), the two calls a graph row needs:

    interface.dump   -> {"interface": [ {interface, device, l3_device, proto,
                          up, pending, autostart, dynamic, uptime,
                          "ipv4-address": [{address, mask}], "ipv6-address": [...]}, ... ]}
    network.device.status -> { "br-lan": {up, carrier, present, mtu, macaddr,
                          devtype, type, "statistics": {rx_bytes, tx_bytes,
                          rx_packets, tx_packets, rx_errors, tx_errors,
                          rx_dropped, tx_dropped}}, "eth0": {...}, "lo": {...} }

- `network.interface.dump` names interfaces (`lan`, `wan`, `wan6`, `loopback`);
  the kernel device to read counters from is `l3_device` (br-lan for `lan`,
  eth1 for `wan`/`wan6`, lo for `loopback`). Device names and interface names
  are different namespaces — the model accepts either when a caller names a row.
- `network.device.status` answers with an object keyed by device name, not an
  array; counters are plain integers and `present` is false for a device that
  is configured but missing.
- Counters are cumulative since boot: a rate is `(bytes - previous)/elapsed`,
  and a counter that goes backwards means the device was recreated (drop that
  sample instead of reporting a negative rate).

## Config read/write (all core domains)

- `uci` (ubus object): THE staged-apply primitive —
  read -> edit (staged) -> `changes` (list) -> `apply`+`confirm` or `revert`.
  Domains map to UCI packages: system, network, wireless, firewall (fw4),
  dhcp, and sqm (sqm-scripts stores its config in UCI too).
- `luci-rpc.getInitList` etc. for service state.

### Measured uci object on 25.12.6 (rpcd 2026.x, 2026-11-12)

`ubus -v list uci` on the dev VM, which corrects the earlier inventory:

    get      {config, section, option, type, match}
    state    {config, section, option, type, match}
    add      {config, type, name, values}
    set      {config, section, type, match, values}
    delete   {config, section, option, options}
    rename   {config, section, option, name}
    order    {config, sections}
    changes  {config}
    revert   {config}
    commit   {config}
    apply    {rollback, timeout}
    confirm  {}
    rollback {}

**There is no `get_all`.** A whole package is read with `get` + a section type:

    get {config:"sqm", type:"queue"}
      -> {"values": {"eth1": {".anonymous":false, ".type":"queue", ".name":"eth1",
                              ".index":0, "enabled":"0", "interface":"eth1",
                              "download":"85000", "upload":"10000", ...}}}

`get` with `{config, section}` answers `{"values": {...flat options...}}` for one
section, and `get` with `{config, section, option}` answers `{"value": "85000"}`.
Every option is a string — UCI has no types.

`set` takes a **`values` table**, not an option/value pair: the pair form
(`{config, section, option, value}`) answers status 2 INVALID_ARGUMENT. Measured:

    set {"config":"sqm","section":"eth1","values":{"download":"90000"}} -> [0]
    changes {"config":"sqm"} -> {"changes":[["set","eth1","download","90000"]]}

`changes` is an array of tuples: `[op, section, option, value]` for a `set`, and
`[op, section, type]` for a section-level `add`/`delete`.

`apply {"rollback":true,"timeout":30}` writes the staged delta and starts the
rollback timer; `confirm {}` cancels it. `commit {"config":...}` writes without
the rollback window (what the client uses for one-shot lab resets, not for
user-facing apply). `revert {"config":...}` discards the session's staged delta.

### Staging is per ubus session (measured)

Two sessions against the same package do not see each other's staging. Session A
stages `download=93000`; session B then reads `changes` -> `{"changes":[]}` and
`get` -> the committed `87000`, and B's `revert` does not touch A's delta. The
delta is held in rpcd, keyed by session (nothing appears in `/tmp/.uci`).
Consequence: the editor stages in its own session, `apply`/`revert` are always
issued on that same session, and the UI must not assume it can see another
client's pending edits.

### ACL requirement (measured)

The stock LuCI ACL groups grant `ubus.uci` methods
`["get","set","commit","changes","add","apply","confirm","delete","order","rename"]`
— `revert` (and the non-existent `get_all`) are **not** in that list, so a plain
root session gets `{"error":{"code":-32002,"message":"Access denied"}}` for
`uci revert` even though `uci get`/`set` work. The client therefore ships its own
ACL group, `scripts/router-console.acl.json`, installed as
`/usr/share/rpcd/acl.d/router-console.json` (`scripts/vm-rpcd-acl.sh` on the dev
VM); it grants the same packages plus `revert` and `get_all`-free reads. A real
deployment needs the same group in the router image — that is a packaging item,
not a client workaround.

`system.reboot` is used by the persistence test (and later by the tools tier); it
answers immediately and the VM is back in ~20 s.

### Domains

The client's domain registry (`src/config/DomainRegistry.cpp`) names a package,
the section type to enumerate and the editable options, read from the router's
own config. Adding a domain is one registry entry: the editor, the tree and the
staged-changes list are generic over it. Six domains, in registry order:

| id | package | section type | sections on the dev VM | options |
| --- | --- | --- | --- | --- |
| sqm | sqm | queue | 1 (`eth1`) | enabled, interface, download, upload, qdisc, script, ingress_ecn, egress_ecn, linklayer, use_mq |
| system | system | system | 1 (anonymous) | hostname, timezone, zonename, log_size |
| network | network | interface | 5 (loopback, lan, wan, wan6, lan2) | device, proto, ipaddr, ip6assign |
| wireless | wireless | wifi-iface | **0 — package absent** | device, mode, ssid, network, encryption, key |
| firewall | firewall | zone + rule + redirect | 2 zones (cfg02dc81 = lan, cfg03dc81 = wan) + 9 rules + 0 redirects | zone: name, network, input, output, forward, masq, mtu_fix; rule: name, enabled, chain (derived), target, proto, src, dest, src_ip, dest_ip, src_port, dest_port, icmp_type, family, limit; redirect: name, enabled, target, proto, src, dest, src_dport, dest_ip, dest_port, src_ip, src_port, family, reflection |
| dhcp | dhcp | dhcp | 2 (lan, wan) | interface, start, limit, leasetime, dhcpv4, dhcpv6, ra, ignore |

Section counts and option names are what the VM actually stores (network,
firewall and dhcp come straight from `/etc/config/{network,firewall,dhcp}`; the
SQM choice lists from `/var/run/sqm/available_qdiscs` and `/usr/lib/sqm/*.qos`).

`tests/uci_domain_staged.cpp` enforces that: it reads each package
independently and fails if the section count differs or if any registry field
names an option the package does not hold. A domain may enumerate several
section types — the firewall domain reads `zone`, `rule` and `redirect` and
picks the field set from the selected section's type. The registry check above
covers the primary type; the rule and redirect fields are the ones fw4's own
`parse_rule`/`parse_redirect` accept (`/usr/share/ucode/fw4.uc`). fw4 has no
`chain` option: it derives input/forward/output from `src`/`dest`, so the
WinBox-style chain row is computed and read-only.

**Field kinds beyond text/choice.** `FieldKind::InterfaceRef` fills its choices
from live `network.interface.dump` names and their devices, and stays editable
so a value outside the list can still be typed (DHCP and SQM `interface`, the
network `device`, the firewall zone interface list). `FieldKind::ZoneRef` fills
its choices from the zone names the loaded firewall package itself declares.
`FieldKind::Chain` is the derived read-only WinBox row above.

**Wireless on the dev VM.** There is no `wireless` package and no
`/etc/config/wireless` on the VM (`ubus call uci configs` does not list it; `uci
get wireless` answers "Not found"), because the x86 image has no radio and no
`wpad`/`hostapd`. The domain therefore loads as an **empty** domain: `uci
get {config:"wireless", type:"wifi-iface"}` answering NOT_FOUND is treated as a
package the router does not have (`ConfigEditor` sets a loaded, zero-section
state with the status "no /etc/config/wireless on this router"), not as a read
failure. The client shows no radio and invents none. The wireless field names
are the UCI `wifi-iface` schema OpenWrt writes when a radio exists — they are
the one set in this table not observed on the VM, and the test skips the
option-presence check for an absent package instead of pretending to verify it.

**List-valued options.** `uci get` answers a repeated option (`list ipaddr`,
`list network`, `list icmp_type`) as a JSON array; the decoder joins the
elements with spaces, which is also what `uci get` prints, so the editor renders
`lan lan2` rather than an empty row. On apply the editor splits that value and
sends the array form, so `list network` round-trips as a list; `firewall.zone.network`
and the rules' `icmp_type`/address/port lists are editable.

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

## Daily-driver tier: log, DHCP leases, config backup

Measured on 25.12.6 (2026-11-13, private VM instance on 127.0.0.1:20443).

### Syslog

- The ubus `log` object exists (`log read {lines, stream, oneshot}`), but on this
  image `ubus call log read '{"lines":5}'` answers **empty** — the object's read
  path needs the live `/dev/log` socket consumer, and the buffer `logread` reads
  is not what it returns. Measured, both with `oneshot` and without.
- `logread -f` follows the socket and never returns, so rpcd `file exec` would
  never answer. Working surface: **`file exec /sbin/logread -l <N>`**, a bounded
  window of N complete lines (~20 KB of stdout for N=200, immediate reply).
- The window is polled (2 s) and diffed client-side: `logread` has no sequence
  number, so new lines are the poll's window tail after the largest overlap with
  the previous window. A window with no overlap (log rolled, or a long gap) is
  taken whole — the UI never claims to be lossless.
- Line shape: ctime header (`Thu Nov 12 14:42:57 2026`), then
  `<facility>.<level>`, then `source: message` (`kern.notice kernel: [ 0.000000]
  ...`, `authpriv.info dropbear[5575]: ...`). A line with no such header is kept
  whole as the message.
- ACL: `/sbin/logread` needs `exec` in the read section.

### DHCP leases

- Two surfaces, merged into one table:
  - **live leases** — `luci-rpc getDHCPLeases {}` →
    `{"dhcp_leases":[{ipaddr,macaddr,hostname,expires,interface,duid,iaid}...],
    "dhcp6_leases":[{ip6addrs[],macaddr,duid,iaid,hostname,expires}...]}`.
    Field names confirmed against LuCI's own consumer,
    `/www/luci-static/resources/view/status/include/40_dhcp.js`. `expires` is
    `false` for "never" and the remaining seconds otherwise.
  - **reservations** — `uci get {"config":"dhcp","type":"host"}` (section type
    `host`, `name`/`ip`/`mac`/`duid`). Reservations are config, not leases, so
    LuCI's lease surface does not list them; the client merges them and marks the
    row `reservation`.
- `dhcp ipv6leases` exists but answers per-device empty lease arrays on a VM with
  no IPv6 client; `odhcpd` is not exposed as its own ubus object; the dnsmasq
  object only has `metrics`.
- **No dynamic leases exist on the dev VM, and that is a measured property of the
  lab, not a client bug**: `/tmp/dhcp.leases` (the dnsmasq leasefile) is 0 bytes.
  A DHCP client on br-lan gets its offer from QEMU's slirp DHCP
  (`udhcpc -i br-lan` → `lease of 192.168.77.100 obtained from 192.168.77.2`),
  which does not write dnsmasq's leasefile. `scripts/vm-m12-fixtures.sh` installs
  one `config host` reservation so the table has a stable row to assert on; the
  live-lease parse path is covered by the JSON-shape test in
  `tests/m12_archive_codec.cpp`.
- ACL: `luci-rpc getDHCPLeases` (read), `uci` already grants `get` on `dhcp`.

### Config backup / restore

- Config set: **`uci configs`** (read `configs` ACL) answers
  `{"configs":["network","dhcp",...]}`; each file is read with `file read
  {path:"/etc/config/<name>", base64:true}`.
- Manifest: `file exec /usr/bin/apk list --installed` (226 lines on the VM).
  A router without apk still backs up, with the reason recorded in MANIFEST.txt.
- Archive: `.tar.gz` built client-side — ustar headers plus a gzip container whose
  deflate stream uses **stored blocks**. `gzip -t`, `file` and `tar -tzf` all read
  it (verified); the config set is tens of kilobytes, so no compression step and
  no zlib dependency is worth it. Member names are relative and validated
  (`..`, leading `/`, empty segments rejected).
- Restore stages, never applies: files go to
  `/tmp/router-console-restore/etc/config/<name>`, the window lists every staged
  path with its size, and only an explicit confirm writes them to
  `/etc/config/<name>`. Router recovery from a bad restore is out of scope.
- **`file write` does not create parent directories** (measured: writing
  `/tmp/router-console-restore/etc/config/x` on a missing tree answers
  `Not found`), so staging runs `file exec /bin/mkdir -p` first. ACL: `/bin/mkdir`
  `exec`.
- Writes are per file, so a restore of N configs is N `file write` calls; the tool
  issues them one at a time and reports each.
## Routing (FRR): status windows and the frr.conf editor

Measured on FRR 10.4.1 from the 25.12.6 feed (docs/frr-routing.md has the full
inventory). All reads are rpcd `file.exec` of vtysh:

| window | call | answer |
| --- | --- | --- |
| Routes | `/usr/bin/vtysh -c "show ip route json"` + `"show ipv6 route json"` | prefix → array of {protocol, distance, metric, selected, installed, uptime, nexthops[]} |
| Routes | `/sbin/ip -6 route` | kernel IPv6 routes FRR does not carry (unreachable/blackhole) |
| OSPF | `vtysh -c "show ip ospf neighbor json"` | neighbors → `{"<router-id>":[{nbrState,upTime,deadTime,ifaceAddress,ifaceName}]}` |
| OSPF | `vtysh -c "show ip ospf interface json"` | interface → {area, cost, state, priority, nbrCount, nbrAdjacentCount, ipAddress} |
| OSPF | `vtysh -c "show ip ospf database"` | text: per-area LSA blocks, counted |
| BGP | `vtysh -c "show bgp summary json"` | `ipv4Unicast` → {routerId, as, ribCount, peers{addr:{remoteAs,state,pfxRcd,pfxSnt,...}}} |
| daemons | `vtysh -c "show daemons"` + `file.read /etc/frr/daemons` | running list, enabled flags, package present |

The config editor's `frr` domain reads and writes `/etc/frr/frr.conf` directly
(`file.read`/`file.write`, standard base64 form). There is **no UCI package and
no procd service** for FRR on OpenWrt — `ubus call service list {"name":"frr"}`
answers `{}` — so apply = write `frr.conf.bak` + write `frr.conf` + restart via
`/bin/sh -c '/etc/init.d/frr restart > /tmp/frr-restart.log 2>&1'` (direct
`file.exec` of the init script times out: the daemons hold the pipe open).

ACL: `file` read `exec` on `/usr/bin/vtysh`, `/sbin/ip`, `/usr/libexec/ip-tiny`;
write `write` on `/etc/frr/**` and `exec` on `/etc/init.d/frr`.

## Custom app surface (the plugin contract)

Each custom LuCI app exposes its data one of two ways; the client plugin
declares which in its manifest (docs/plugins.md):
1. A ubus namespace (rpcd object), called with the session the client holds.
2. An HTTP JSON endpoint under /luci-static/, fetched over the pinned TLS
   connection (modemaim serves towers.json this way; its views fetch() it).

### Measured per app (dev VM fixtures, 2026-11-12)

| app | surface | answer |
| --- | --- | --- |
| rist2rist | `service list {"name":"rist2rist"}` | `{ "rist2rist": { "instances": { "instance1": { "running": true, "pid": ... } } } }`; `[0]` with no object when not running |
| rist2rist | `uci get {"config":"rist2rist"}` | `{"values":{"main":{".type":"rist2rist","listen_url":...,"profile":...},"wan_a":{".type":"destination","address":...,"weight":...}}}` |
| modemaim | `modemaim status` | `ts,state,pstate,plmn,operator,tech,mapped{site,conf,source},rssi,rsrp,rsrq,sinr,serving{pci,earfcn,rsrp,rsrq,ta,ta_us,eci},neighbors[],atNeighbors[]` |
| modemaim | `modemaim origin` | `{"lat":...,"lon":...,"confirmed":...}` |
| modemaim | `GET /luci-static/resources/modemaim/towers.json` | `{source,generated,data_date,bbox,towers[{site,lat,lon,band,mhz,bw,eirp,h,azs,hbw,carrier}]}` — static by design: over the 1 MiB ubus cap rpcd drops the whole object |
| nettime | `nettime status` (the client's shim) | `{ts,enabled,port,clock,provider,uptime,clients}` — **mock**: the packaged app exposes only the UDP 5637 GstNetTime provider, no rpcd object |

The LuCI apps are not installed in the VM (no OpenWrt SDK build in this
milestone); `scripts/vm-mock-apps.sh` installs a mock rpcd script for modemaim
and nettime, a procd stand-in + `/etc/config/rist2rist` for rist2rist, and a
small tower extract with the real schema. modemaim's mock answers the real
script's keys verbatim; the nettime one does not correspond to any shipped method.

The plugin surfaces ship as their own rpcd ACL group (`scripts/plugins.acl.json`,
installed as `/usr/share/rpcd/acl.d/router-console-plugins.json`): `service list`,
`modemaim status/origin/bearing/towers`, `nettime status`, and read access to
the `rist2rist` and `modemaim` UCI packages. It is a separate file on purpose —
`scripts/router-console.acl.json` carries the core domains and the tool tier, and
the two are edited independently.

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
