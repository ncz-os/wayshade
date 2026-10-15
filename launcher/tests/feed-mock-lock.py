#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# feed-mock-lock.py - the LOCK side of the lock_framesrc protocol, for testing
# ncz-screensaver-feed without a compositor. Emulates lock_framesrc.c: accepts
# one connection, sends HELLO/WELCOME, validates every FRAME it receives
# (magic, geometry, stride, fd size), acks with READY, and after --frames sends
# STOP. Writes a JSON result and exits 0 on success, 1 on any protocol fault.
#
# Usage: feed-mock-lock.py --socket PATH --width W --height H --scale S
#                          --frames N --result FILE [--timeout SEC]
import argparse
import array
import json
import os
import socket
import struct
import sys
import time

LFS_MAGIC = 0x4C465331
LFS_VERSION = 1
LFS_HELLO, LFS_FRAME, LFS_BYE = 1, 2, 3
LFS_HOLD = 4
LFS_WELCOME, LFS_READY, LFS_STOP = 0x80, 0x81, 0x82
# WELCOME message-header capability bit (see include/lock_framesrc_proto.h). A
# HOLD-aware lock sets it; a lock built before HOLD existed sends 0.
LFS_FLAG_HOLD = 0x0001
_MSG = struct.Struct("<IHHI")
_HELLO = struct.Struct("<III")
_WELCOME = struct.Struct("<III")
_OUTPUT = struct.Struct("<IIII")
_FRAME = struct.Struct("<IIIIIIIII")
_ACK = struct.Struct("<I")


def name_hash(s):
    h = 2166136261
    for ch in s.encode():
        h ^= ch
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def send(sock, mtype, payload=b"", flags=0):
    sock.sendmsg([_MSG.pack(LFS_MAGIC, mtype, flags, len(payload)) + payload])


def recv(sock):
    data, anc, _fl, _a = sock.recvmsg(4096, socket.CMSG_SPACE(4 * 4))
    fds = []
    for level, ctype, cdata in anc:
        if level == socket.SOL_SOCKET and ctype == socket.SCM_RIGHTS:
            arr = array.array("i")
            arr.frombytes(cdata[: len(cdata) - (len(cdata) % 4)])
            fds.extend(arr.tolist())
    return data, fds


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--socket", required=True)
    ap.add_argument("--width", type=int, default=320)
    ap.add_argument("--height", type=int, default=240)
    ap.add_argument("--scale", type=int, default=1)
    ap.add_argument("--outputs", default="",
                    help='comma list of WxH@S (overrides --width/--height/--scale)')
    ap.add_argument("--frames", type=int, default=8)
    ap.add_argument("--hold", action="store_true",
                    help="a HOLD is expected (freeze policy); stop at the HOLD")
    ap.add_argument("--old-lock", action="store_true",
                    help="emulate a lock built BEFORE LFS_HOLD existed: send a "
                         "WELCOME with flags=0 (no LFS_FLAG_HOLD) and treat any "
                         "HOLD as a fault. A conforming feed must then degrade "
                         "(low fps, no HOLD) instead of looping on 'unknown "
                         "message type'.")
    ap.add_argument("--allow-static", action="store_true",
                    help="do not require >=2 distinct frames (warm-up/keepalive test)")
    ap.add_argument("--require-first-within", type=float, default=0.0,
                    help="fail unless the FIRST frame arrives within this many seconds "
                         "(the lock's 3 s frame watchdog budget)")
    ap.add_argument("--pause-at", type=int, default=0,
                    help="after this many frames send PAUSE (needs the saver's LFS_FLAG_PAUSE)")
    ap.add_argument("--pause-s", type=float, default=3.0,
                    help="seconds to stay paused; any frame in that window is an error")
    ap.add_argument("--pause-marker", default="",
                    help="file created while paused (lets the test inspect the renderer)")
    ap.add_argument("--require-nonblack", action="store_true",
                    help="fail unless at least one frame has visible (non-black) pixels")
    ap.add_argument("--result", required=True)
    ap.add_argument("--timeout", type=float,
                    default=float(os.environ.get("NCZ_TEST_TIMEOUT", "45")))
    a = ap.parse_args()

    # Output list: (hash, buffer_w, buffer_h, scale_120).
    outs = []
    if a.outputs:
        for i, spec in enumerate(a.outputs.split(",")):
            spec = spec.strip()
            wh, _at, sc = spec.partition("@")
            w, _x, h = wh.partition("x")
            outs.append((name_hash(f"mock-{i}"), int(w), int(h), int(sc or 1) * 120))
    else:
        outs.append((name_hash("mock-0"), a.width, a.height, a.scale * 120))

    res = {"frames": 0, "errors": [], "sizes": [], "seen": {}, "hold": False,
           "first_frame_s": None}
    t0 = time.monotonic()
    try:
        os.unlink(a.socket)
    except OSError:
        pass
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    srv.bind(a.socket)
    srv.listen(1)
    srv.settimeout(a.timeout)
    conn = None
    try:
        conn, _ = srv.accept()
        # HELLO
        data, _ = recv(conn)
        _magic, mtype, _f, ln = _MSG.unpack_from(data, 0)
        if mtype != LFS_HELLO:
            raise SystemExit(f"expected HELLO, got {mtype}")
        if len(data) < _MSG.size + _HELLO.size:
            raise SystemExit("short HELLO")
        abi, transports, _n = _HELLO.unpack_from(data, _MSG.size)
        res["pause_capable"] = bool(_f & 0x0002)
        if abi != LFS_VERSION or not (transports & 0x1):
            raise SystemExit("bad HELLO abi/transports")
        payload = _WELCOME.pack(LFS_VERSION, 0x1, len(outs))
        for (h, w, hh, s120) in outs:
            payload += _OUTPUT.pack(h, w, hh, s120)
        # A HOLD-aware lock advertises the capability bit; --old-lock emulates a
        # pre-LFS_HOLD lock, which sends flags=0.
        send(conn, LFS_WELCOME, payload,
             flags=0 if a.old_lock else LFS_FLAG_HOLD)
        by_hash = {h: (h, w, hh, s120) for (h, w, hh, s120) in outs}
        # frames
        expect = 0
        deadline = time.monotonic() + a.timeout
        while res["frames"] < a.frames:
            if time.monotonic() > deadline:
                res["errors"].append("timeout waiting for frames")
                break
            conn.settimeout(max(0.1, deadline - time.monotonic()))
            try:
                data, fds = recv(conn)
            except socket.timeout:
                res["errors"].append("timeout")
                break
            if not data:
                res["errors"].append("connection closed")
                break
            if len(data) < _MSG.size:
                res["errors"].append("short header")
                break
            magic, mtype, _f, ln = _MSG.unpack_from(data, 0)
            if magic != LFS_MAGIC:
                res["errors"].append("bad magic")
                break
            if mtype == LFS_STOP:
                break
            if mtype == LFS_HOLD:
                res["hold"] = True
                if a.old_lock:
                    res["errors"].append(
                        "old lock received a HOLD: the feed ignored the missing "
                        "WELCOME capability bit")
                break
            if mtype != LFS_FRAME:
                res["errors"].append(f"unexpected type {mtype}")
                break
            if mtype == LFS_FRAME and res["first_frame_s"] is None:
                res["first_frame_s"] = time.monotonic() - t0
            if len(data) < _MSG.size + _FRAME.size:
                res["errors"].append("short FRAME")
                break
            f = _FRAME.unpack_from(data, _MSG.size)
            seq, oh, w, hh, stride, offset, fmt = f[0], f[1], f[2], f[3], f[4], f[5], f[6]
            if seq != expect:
                res["errors"].append(f"seq {seq} != {expect}")
                break
            if oh not in by_hash:
                res["errors"].append(f"FRAME for unknown output {oh:#x}")
                break
            _h, ew, eh, _s = by_hash[oh]
            if (w != ew or hh != eh or stride < w * 4 or fmt not in (0, 1)):
                res["errors"].append(
                    f"frame geometry {w}x{hh} (want {ew}x{eh}) stride={stride} fmt={fmt}")
                break
            res["seen"][f"{oh:#x}"] = res["seen"].get(f"{oh:#x}", 0) + 1
            if not fds:
                res["errors"].append("FRAME without fd")
                break
            fd = fds[0]
            sz = os.fstat(fd).st_size
            if sz < offset + stride * hh:
                res["errors"].append(f"fd too small {sz} < {offset + stride * hh}")
                os.close(fd)
                break
            # Sample the frame content: a real animated hack produces distinct
            # frames, whereas the feed's warm-up "hold" frames are all black.
            try:
                import mmap as _mmap
                mm = _mmap.mmap(fd, sz, prot=_mmap.PROT_READ)
                hv = 0
                for off in range(0, min(sz, 1 << 22), 4096):
                    hv = (hv * 31 + mm[off]) & 0xFFFFFFFF
                    if off + 2 < sz and max(mm[off], mm[off + 1], mm[off + 2]) > 8:
                        res["nonblack"] = True
                mm.close()
                res.setdefault("hashes", [])
                if hv not in res["hashes"]:
                    res["hashes"].append(hv)
            except (OSError, ValueError):
                pass
            res["sizes"].append(sz)
            os.close(fd)
            for extra in fds[1:]:
                os.close(extra)
            send(conn, LFS_READY, _ACK.pack(seq))
            expect += 1
            res["frames"] += 1
            if a.pause_at and res["frames"] == a.pause_at:
                if not res.get("pause_capable"):
                    res["errors"].append("saver did not advertise LFS_FLAG_PAUSE")
                    break
                send(conn, 0x83, b"")                      # LFS_PAUSE
                if a.pause_marker:
                    open(a.pause_marker, "w").close()
                end = time.monotonic() + a.pause_s
                during = 0
                while True:
                    left = end - time.monotonic()
                    if left <= 0:
                        break
                    conn.settimeout(left)
                    try:
                        d2, f2 = recv(conn)
                    except socket.timeout:
                        break
                    for x in f2:
                        os.close(x)
                    if d2 and len(d2) >= _MSG.size and _MSG.unpack_from(d2, 0)[1] == LFS_FRAME:
                        during += 1
                        # still ack so the protocol stays consistent
                        fs = _FRAME.unpack_from(d2, _MSG.size)
                        send(conn, LFS_READY, _ACK.pack(fs[0]))
                        expect += 1
                res["frames_while_paused"] = during
                if during > 1:   # one frame may already have been in flight
                    res["errors"].append(f"{during} frames arrived while paused")
                if a.pause_marker:
                    try:
                        os.unlink(a.pause_marker)
                    except OSError:
                        pass
                send(conn, 0x84, b"")                      # LFS_RESUME
                res["resumed_at_frame"] = res["frames"]
        send(conn, LFS_STOP, _ACK.pack(expect))
        time.sleep(0.2)
    finally:
        if conn is not None:
            try:
                conn.close()
            except OSError:
                pass
        srv.close()
    if a.pause_at and res["frames"] <= res.get("resumed_at_frame", 1 << 30):
        res["errors"].append("no frame after RESUME")
    if a.require_nonblack and not res.get("nonblack"):
        res["errors"].append("every frame was black")
    if a.hold and not res["hold"]:
        res["errors"].append("expected a HOLD, none arrived")
    if not a.hold and res["hold"]:
        res["errors"].append("unexpected HOLD (freeze not requested)")
    if a.require_first_within > 0:
        ff = res["first_frame_s"]
        if ff is None:
            res["errors"].append(
                f"no frame within {a.require_first_within}s (watchdog budget)")
        elif ff > a.require_first_within:
            res["errors"].append(
                f"first frame at {ff:.2f}s > {a.require_first_within}s (watchdog budget)")
    with open(a.result, "w") as fh:
        json.dump(res, fh)
    distinct = len(res.get("hashes", []))
    # A missing HOLD (when one was expected) and an unexpected HOLD are already
    # recorded in res["errors"] above, so `not res["errors"]` covers them.
    ok = (not res["errors"] and res["frames"] > 0
          and (a.allow_static or distinct >= 2)
          and len(res["seen"]) == len(outs))
    print(f"mock-lock: frames={res['frames']} outputs={len(outs)} "
          f"hold={res['hold']} seen={res['seen']} distinct={distinct} "
          f"first_frame_s={res['first_frame_s']} errors={res['errors']}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
