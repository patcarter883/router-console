# Routing on OpenWrt: the measured FRR surface

Everything here was measured on the lab VM (OpenWrt 25.12.6, x86-64, kernel
6.12.112) with the packages OpenWrt actually ships. None of it is inferred from
FRR on Debian: the OpenWrt packaging differs in three ways that matter (one
config file, no UCI package, no procd service).

## Packages and layout (measured: `apk add frr frr-ospfd frr-bgpd frr-staticd`)

| path | what it is |
| --- | --- |
| `/usr/bin/vtysh` | the CLI, shipped by the `frr` package — there is **no** `frr-vtysh` package |
| `/usr/sbin/{zebra,ospfd,bgpd,staticd,mgmtd,watchfrr}` | the daemons |
| `/etc/frr/frr.conf` | **the** config file: every daemon's sections in one file |
| `/etc/frr/daemons` | which daemons start (`bgpd=no`, `ospfd=no` by default) |
| `/etc/frr/vtysh.conf` | vtysh's own file; `service integrated-vtysh-config` is commented out |
| `/var/run/frr/*.vty`, `*.pid` | vtysh sockets and pidfiles |

`/etc/config/frr` does **not** exist: FRR is not UCI-managed on OpenWrt. There is
no `uci set frr.*` and no `uci apply/confirm` for routing.

### Daemon start rules (from `/etc/frr/daemons` itself)

    # The staticd,watchfrr and zebra daemons are always started.

so zebra, staticd, mgmtd and watchfrr have no toggle; `bgpd` and `ospfd` start
only with `bgpd=yes` / `ospfd=yes`. Enabling a protocol therefore means editing
`/etc/frr/daemons` and restarting — the client detects the flags but never
rewrites that file (a deliberate scope limit: the config editor owns `frr.conf`,
the operator owns the enable list).

### No procd service (measured)

    ubus call service list '{"name":"frr"}'   ->   { }

`/etc/init.d/frr` is an old-style `rc.common` script that sources
`/usr/sbin/frrcommon.sh` and starts `watchfrr` directly; it registers nothing
with procd, so there are no per-daemon procd instances to enumerate either.
Daemon detection is therefore:

1. `/etc/frr/daemons` — present at all (package installed), and `=yes` flags;
2. `vtysh -c "show daemons"` — the authoritative runtime answer, e.g.
   ` mgmtd zebra ospfd bgpd watchfrr staticd`;
3. `zebra` in that list — frr itself is up.

`Routing::Daemons` in `src/protocol/RoutingTypes.h` carries exactly those two
lists plus `installed`.

## vtysh through rpcd

`file.exec` of `/usr/bin/vtysh -c "<one command>"` works non-interactively and
returns the rendered text in `stdout`; exit code 0. It must be one command per
call (`-c` chains also work, but one call per query keeps the parsers honest).
`show daemons json` does **not** exist (`% Unknown command`), so that one surface
is text.

ACL additions (`scripts/router-console.acl.json`, verified by
`scripts/vm-rpcd-acl.sh`):

    read.file: "/usr/bin/vtysh": ["exec"]
               "/sbin/ip": ["exec"]            (symlink, resolved to /usr/libexec/ip-tiny)
               "/usr/libexec/ip-tiny": ["exec"]
    write.file: "/etc/frr/**": ["write"]        (frr.conf and frr.conf.bak)
                "/etc/init.d/frr": ["exec"]

### Restarting through rpcd hangs unless stdout is redirected (measured)

`ubus call file exec '{"command":"/etc/init.d/frr","params":["restart"]}'` times
out: the daemons forked by the init script inherit rpcd's stdout pipe, so rpcd
never sees EOF and gives up after its 30 s timeout. The client therefore runs

    /bin/sh -c '/etc/init.d/frr restart > /tmp/frr-restart.log 2>&1; echo $?'

and reads `/tmp/frr-restart.log` afterwards. That also captures frr's own
config-processing errors, which are the only signal that a syntactically wrong
`frr.conf` was rejected (the init script still exits 0).

## JSON status shapes (FRR 10.4.1, measured)

All three have JSON; the client parses JSON and never scrapes the tables.

| command | shape |
| --- | --- |
| `show ip route json` | `{"<prefix>":[{prefix,prefixLen,protocol,distance,metric,selected,installed,uptime,nexthops:[{ip,interfaceName,directlyConnected,active}]}]}` — one array element per protocol offering the prefix |
| `show ipv6 route json` | same, keyed by IPv6 prefix; an entry's own `prefix` field can read `"::/0 from fec0::/64"`, so the **key** is the prefix to display |
| `show ip ospf neighbor json` | `{"neighbors":{"<router-id>":[{nbrState,nbrPriority,upTime,deadTime,ifaceAddress,ifaceName,converged,role,...}]}}` — note the array per neighbor id, and `ifaceName` is `"eth2:10.99.0.1"` (interface:local address) |
| `show ip ospf interface json` | `{"interfaces":{"<ifname>":{area,routerId,networkType,state,cost,priority,nbrCount,nbrAdjacentCount,ifUp,ipAddress,ipAddressPrefixlen,...}}}` |
| `show ip ospf database json` | exists; the client parses the text form instead because the window shows per-area LSA **counts** (`show ip ospf database` prints `Router Link States (Area 0.0.0.0)` + a column header + one line per LSA) |
| `show bgp summary json` | keyed by address family: `{"ipv4Unicast":{routerId,as,ribCount,peerCount,peers:{"<addr>":{remoteAs,localAs,state,pfxRcd,pfxSnt,msgRcvd,msgSent,peerUptime,...}}}}` |

`ip -6 route` (via `/sbin/ip`) fills in kernel routes FRR does not carry
(`unreachable`/`blackhole` and policy blocks); the client merges them by
(prefix, protocol) so nothing is listed twice.

## frr.conf grammar (measured)

The file is a flat sequence of blocks separated by `!`:

    frr version 10.4.1
    frr defaults traditional
    hostname openwrt-a
    log syslog informational
    !
    interface br-lan
     ip ospf area 0.0.0.0
    !
    router ospf
     ospf router-id 192.168.77.1
    !
    router bgp 65001
     bgp router-id 192.168.77.1
     network 192.168.77.0/24
     neighbor 10.99.0.2 remote-as 65001
     neighbor 10.99.0.2 update-source eth2
    !
    line vty
    !

Rules the editor encodes (`src/protocol/FrrCodec.cpp`):

- An indented line is a statement; a top-level line whose first word is a block
  keyword (`router`, `interface`, `line`, `route-map`, `ip`, `access-list`, ...)
  opens a block; any other top-level line is a global one-liner.
- A statement is split as `<keyword> <value>` at the **last** space: `ospf
  router-id 1.2.3.4` → `("ospf router-id", "1.2.3.4")`, `neighbor 10.99.0.2
  remote-as 65001` → `("neighbor 10.99.0.2 remote-as", "65001")`. Editing a row
  rewrites exactly `"<keyword> <value>"`, so untouched lines round-trip byte for
  byte, and a keyword that repeats in one block is shown read-only instead of
  being silently collapsed.
- Interface-level `ip ospf area` and router-level `network ... area` are mutually
  exclusive in FRR 10: adding both makes ospfd refuse the file with
  `Please remove all ip ospf area x.x.x.x commands first.` The lab config uses
  the interface form, which is what modern FRR expects.

## Apply / revert semantics (and what they are not)

FRR has no UCI-style staged apply with a confirm window. The editor keeps the
same **client-side** staged diff (nothing touches the router until Apply), and
Apply maps to:

1. `file write /etc/frr/frr.conf.bak` with the committed text;
2. `file write /etc/frr/frr.conf` with the rendered staged text;
3. `/etc/init.d/frr restart` through the shell redirect above;
4. `vtysh -c "show daemons"` probe — success requires exit 0, no
   `processing failure` / `Failure to communicate` in the restart output, and
   `zebra` in the running list.

Revert is therefore two different things depending on state, and the window says
which one it did:

- nothing applied yet: discard the local staging, no router write;
- an apply already landed in this session: restore `frr.conf.bak` and restart,
  which is the only undo available.

This is honest but weaker than UCI's confirm/rollback timer: if the new config
starts frr with a broken daemon set, the operator must press Revert themselves.
`tests/routing_views.cpp --mode config` exercises both halves (stage → diff →
discard, then apply → assert the router kept the value → revert → assert the
baseline file is back).

## Verification

Two private lab VMs (QEMU `-netdev socket` segment, like the mDNS relay pair)
with `ospfd` + `bgpd` on both, iBGP AS 65001 across the segment:

    show ip ospf neighbor  ->  192.168.77.2  1  Full/DR  ...  10.99.0.2  eth2:10.99.0.1
    show bgp summary       ->  1 peer, Established, pfxRcd 1, pfxSnt 1
    show ip route          ->  B 10.0.2.0/24 via 10.99.0.2 (learned from the peer)

The single-router case is the normal one for a live fleet box: OSPF shows its
interfaces in `Waiting`/`DR` with no neighbors, BGP shows configured peers in
`Active` — both windows render that state rather than an empty table, and the
tests only require `Full` when a neighbor exists.
