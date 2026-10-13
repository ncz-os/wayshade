#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# fake-slow-child.py - a stand-in off-screen hack for the feed's warm-up tests.
#
# It speaks the saver side of the lock_framesrc child protocol exactly like a
# real hack in NCZ_FEED_SOCKET mode: connect, HELLO, read WELCOME. By default it
# then stays SILENT (the "slow warm-up" case: shader compile / EGL init takes
# longer than the lock's 3 s frame watchdog, exactly what happened on the CIX
# Mali blob before the feed's black-keepalive path kept the lock fed).
#
# FAKE_CHILD_DELAY   seconds to stay silent before the first FRAME (default: never)
# FAKE_CHILD_FRAMES  number of real (distinct, non-black) FRAMEs to send after the
#                    delay, honouring one-buffer-in-flight via LFS_READY (default 0)
import os
import socket
import struct
import sys
import time

LFS_MAGIC = 0x4C465331
LFS_VERSION = 1
LFS_HELLO, LFS_FRAME = 1, 2
LFS_WELCOME, LFS_READY, LFS_STOP = 0x80, 0x81, 0x82
_MSG = struct.Struct("<IHHI")
_HELLO = struct.Struct("<III")
_WELCOME = struct.Struct("<III")
_OUTPUT = struct.Struct("<IIII")
_FRAME = struct.Struct("<IIIIIIIII")


def send(sock, mtype, payload=b"", fd=None):
    if fd is None:
        sock.sendmsg([_MSG.pack(LFS_MAGIC, mtype, 0, len(payload)) + payload])
        return
    import array
    fds = array.array("i", [fd])
    sock.sendmsg([_MSG.pack(LFS_MAGIC, mtype, 0, len(payload)) + payload],
                 [(socket.SOL_SOCKET, socket.SCM_RIGHTS, fds)])


def main():
    path = os.environ["NCZ_FEED_SOCKET"]
    delay = float(os.environ.get("FAKE_CHILD_DELAY", "1e9"))
    nframes = int(os.environ.get("FAKE_CHILD_FRAMES", "0"))
    s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    for _ in range(400):
        try:
            s.connect(path)
            break
        except OSError:
            time.sleep(0.05)
    else:
        sys.exit(4)
    send(s, LFS_HELLO, _HELLO.pack(LFS_VERSION, 0x1, 0))
    # Read WELCOME (one message).
    data, _anc, _fl, _a = s.recvmsg(4096, socket.CMSG_SPACE(16))
    if len(data) < _MSG.size:
        sys.exit(4)
    _m, mtype, _f, _ln = _MSG.unpack_from(data, 0)
    if mtype != LFS_WELCOME:
        sys.exit(4)
    off = _MSG.size + _WELCOME.size
    oh, w, h, _s = _OUTPUT.unpack_from(data, off)
    stride = w * 4
    sys.stderr.write(f"fake-slow-child: WELCOME {w}x{h} delay={delay} frames={nframes}\n")
    sys.stderr.flush()

    # Default (delay >= 3600) is the "silent child" case: send no frame at all.
    if delay < 3600.0:
        time.sleep(delay)
    seq = 0
    while seq < nframes:
        mfd = os.memfd_create("fake-frame", getattr(os, "MFD_CLOEXEC", 0))
        os.ftruncate(mfd, stride * h)
        buf = bytearray(stride * h)
        # distinct, non-black content per frame
        for y in range(0, h, 17):
            base = y * stride
            for x in range(0, w, 1):
                v = (seq * 7 + x + y) & 0xFF
                buf[base + x * 4 + 0] = v
                buf[base + x * 4 + 1] = (v + 40) & 0xFF
                buf[base + x * 4 + 2] = (v + 80) & 0xFF
                buf[base + x * 4 + 3] = 0xFF
        os.write(mfd, bytes(buf))
        fm = _FRAME.pack(seq, oh, w, h, stride, 0, 1, 0, 0)
        try:
            send(s, LFS_FRAME, fm, fd=mfd)
        except OSError:
            os.close(mfd)
            break
        os.close(mfd)
        seq += 1
        # wait for READY / STOP
        s.settimeout(5.0)
        try:
            rdata, _a2, _f2, _f3 = s.recvmsg(4096, socket.CMSG_SPACE(16))
        except OSError:
            break
        if not rdata:
            break
        _m2, mt2, _f4, _l2 = _MSG.unpack_from(rdata, 0)
        if mt2 == LFS_STOP:
            break
    # Stay alive but silent until the feed stops us.
    s.settimeout(30.0)
    while True:
        try:
            rdata, _a3, _f5, _f6 = s.recvmsg(4096, socket.CMSG_SPACE(16))
        except OSError:
            break
        if not rdata:
            break
        _m3, mt3, _f7, _l3 = _MSG.unpack_from(rdata, 0)
        if mt3 == LFS_STOP:
            break
    return 0


if __name__ == "__main__":
    sys.exit(main())
