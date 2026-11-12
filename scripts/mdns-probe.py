import socket, struct, sys, time

GROUP = "224.0.0.251"
IFACE = "192.168.78.2"
GUEST = "192.168.78.1"


def enc(n):
    return b"".join(bytes([len(l)]) + l.encode() for l in n.split(".")) + b"\x00"


def read_name(buf, off):
    labels = []
    while True:
        ln = buf[off]
        if ln == 0:
            return ".".join(labels), off + 1
        if ln & 0xC0 == 0xC0:
            ptr = struct.unpack(">H", buf[off:off + 2])[0] & 0x3FFF
            sub, _ = read_name(buf, ptr)
            labels.append(sub)
            return ".".join(labels), off + 2
        labels.append(buf[off + 1:off + 1 + ln].decode(errors="replace"))
        off += 1 + ln


def dump(buf):
    _id, flags, qd, an, ns, ar = struct.unpack(">HHHHHH", buf[:12])
    off = 12
    for _ in range(qd):
        _, off = read_name(buf, off)
        off += 4
    rows = []
    for i in range(an + ns + ar):
        name, off = read_name(buf, off)
        rtype, rclass, ttl, rdlen = struct.unpack(">HHIH", buf[off:off + 10])
        off += 10
        rdata = buf[off:off + rdlen]
        if rtype == 1:
            val = ".".join(str(b) for b in rdata)
        elif rtype == 12:
            val, _ = read_name(buf, off)
        elif rtype == 33:
            prio, weight, port = struct.unpack(">HHH", rdata[:6])
            target, _ = read_name(buf, off + 6)
            val = f"{target}:{port}"
        elif rtype == 16:
            i, txt = 0, []
            while i < len(rdata):
                ln = rdata[i]
                txt.append(rdata[i + 1:i + 1 + ln].decode(errors="replace"))
                i += 1 + ln
            val = " ".join(txt)
        else:
            val = rdata.hex()
        sec = "AN" if i < an else ("NS" if i < an + ns else "AR")
        rows.append(f"    [{sec}] {name} type={rtype} ttl={ttl} -> {val}")
        off += rdlen
    print(f"  header answers={an} authority={ns} additional={ar}")
    print("\n".join(rows))


s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
s.bind(("0.0.0.0", 5353))
s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
             socket.inet_aton(GROUP) + socket.inet_aton(IFACE))
s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(IFACE))
s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
s.settimeout(2.0)

specs = sys.argv[1:] or ["_ssh._tcp.local:12", "_services._dns-sd._udp.local:12",
                         "openwrt-dev.local:1", "openwrt-dev.local:255"]
if specs and specs[0] == "--listen":
    deadline = time.time() + float(sys.argv[2] if len(sys.argv) > 2 else 12)
    print("listening for announcements")
    while time.time() < deadline:
        try:
            data, peer = s.recvfrom(9000)
        except socket.timeout:
            continue
        print(f" packet from {peer[0]}:{peer[1]} ({len(data)} bytes)")
        dump(data)
    raise SystemExit(0)

for spec in specs:
    name, _, qt = spec.partition(":")
    qt = int(qt or 12)
    s.sendto(struct.pack(">HHHHHH", 0, 0, 1, 0, 0, 0) + enc(name) + struct.pack(">HH", qt, 1),
             (GROUP, 5353))
    print(f"=== query {name} qtype={qt}")
    deadline = time.time() + 3
    while time.time() < deadline:
        try:
            data, peer = s.recvfrom(9000)
        except socket.timeout:
            break
        if peer[0] != GUEST:
            continue
        print(f" reply from {peer[0]}:{peer[1]} ({len(data)} bytes)")
        dump(data)
