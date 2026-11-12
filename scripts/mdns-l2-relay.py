#!/usr/bin/env python3
"""Relay QEMU's userspace Ethernet socket onto a host veth pair.

Slirp terminates the guest LAN inside QEMU and never forwards multicast, and
this host's kernel has no tun/tap module, so the lab gets its multicast-capable
segment (192.168.78.0/24) from a veth pair plus this L2 relay: frames from
QEMU's `-netdev socket` go out on the veth's peer end, where the host kernel --
and therefore a QUdpSocket joined to 224.0.0.251 -- sees them.

Usage: mdns-l2-relay.py --iface rcnet-qemu --listen 127.0.0.1:15077
"""
from __future__ import annotations

import argparse
import socket
import struct
import sys
import threading

# PACKET_IGNORE_OUTGOING (Linux >= 4.20): do not report frames this socket sent.
PACKET_IGNORE_OUTGOING = 23
ETH_P_ALL = 0x0003


def log(msg: str) -> None:
    print(f"mdns-l2-relay: {msg}", file=sys.stderr, flush=True)


def recv_exact(conn: socket.socket, n: int) -> bytes | None:
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def pump_qemu_to_veth(conn: socket.socket, raw: socket.socket) -> None:
    while True:
        header = recv_exact(conn, 4)
        if header is None:
            log("qemu closed the socket")
            return
        (length,) = struct.unpack(">I", header)
        if length == 0 or length > 65536:
            log(f"bad frame length {length}")
            return
        frame = recv_exact(conn, length)
        if frame is None:
            log("qemu closed mid-frame")
            return
        try:
            raw.send(frame)
        except OSError as exc:
            log(f"veth write failed: {exc}")


def pump_veth_to_qemu(conn: socket.socket, raw: socket.socket) -> None:
    while True:
        frame = raw.recv(65536)
        try:
            conn.sendall(struct.pack(">I", len(frame)) + frame)
        except OSError as exc:
            log(f"qemu write failed: {exc}")
            return


def serve(iface: str, listen: tuple[str, int]) -> None:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(listen)
    server.listen(1)
    log(f"listening on {listen[0]}:{listen[1]}, bridging to {iface}")
    while True:
        conn, peer = server.accept()
        log(f"qemu attached from {peer[0]}:{peer[1]}")
        raw = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ALL))
        raw.bind((iface, 0))
        try:
            raw.setsockopt(getattr(socket, "SOL_PACKET", 263), PACKET_IGNORE_OUTGOING, 1)
        except OSError:
            pass
        up = threading.Thread(target=pump_veth_to_qemu, args=(conn, raw), daemon=True)
        up.start()
        pump_qemu_to_veth(conn, raw)
        conn.close()
        raw.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iface", default="rcnet-qemu")
    parser.add_argument("--listen", default="127.0.0.1:15077")
    args = parser.parse_args()
    host, _, port = args.listen.rpartition(":")
    serve(args.iface, (host or "127.0.0.1", int(port)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
