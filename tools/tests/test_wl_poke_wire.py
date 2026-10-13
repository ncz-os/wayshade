# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/wl_poke.py.

Two layers of coverage, all running without a real compositor:

  1. Pure wire helper tests -- string/array/fixed encoding, message header
     packing, event decoding from a bytes buffer.
  2. End-to-end Connection tests against a tiny in-process fake Wayland
     server (a thread talking to a socketpair). The fake server answers
     ``wl_display.get_registry`` with a couple of globals and ``wl_display
     .sync`` with a done event, which exercises the bind path and the
     roundtrip path of ``Connection``.

Run with::

    python3 -m unittest tools.tests.test_wl_poke_wire -v
"""

from __future__ import annotations

import os
import socket
import struct
import sys
import threading
import time
import unittest

# Make the tools/ directory importable regardless of where unittest is run.
sys.path.insert(0, os.path.join(os.path.dirname(__file__), os.pardir))

import wl_poke

# ---------------------------------------------------------------------------
# Pure wire helpers.
# ---------------------------------------------------------------------------


class Pad4Tests(unittest.TestCase):
    def test_pad4_zero(self):
        self.assertEqual(wl_poke.pad4(0), 0)

    def test_pad4_one(self):
        self.assertEqual(wl_poke.pad4(1), 4)

    def test_pad4_four(self):
        self.assertEqual(wl_poke.pad4(4), 4)

    def test_pad4_five(self):
        self.assertEqual(wl_poke.pad4(5), 8)

    def test_pad4_large(self):
        self.assertEqual(wl_poke.pad4(33), 36)


class StringTests(unittest.TestCase):
    def test_empty_string(self):
        # Empty string: length=0, no payload, no padding.
        out = wl_poke.pack_string("")
        self.assertEqual(out, struct.pack("<I", 0))

    def test_short_string(self):
        # "ab" -- 2 bytes + NUL = length 3, padded to 4.
        out = wl_poke.pack_string("ab")
        self.assertEqual(out[:4], struct.pack("<I", 3))
        self.assertEqual(out[4:8], b"ab\x00\x00")

    def test_exact_multiple_of_4(self):
        # "abcd" -- 5 bytes incl NUL. 5 is not a multiple of 4, so 3 bytes
        # of padding are appended. This matches what libwayland writes on
        # the wire for a non-empty string.
        out = wl_poke.pack_string("abcd")
        self.assertEqual(out[:4], struct.pack("<I", 5))
        self.assertEqual(out[4:9], b"abcd\x00")
        self.assertEqual(out[9:], b"\x00\x00\x00")

    def test_non_ascii(self):
        # UTF-8 "S\u00e9" = "S" + 0xc3 0xa9 = 3 bytes + NUL = 4.
        out = wl_poke.pack_string("S\u00e9")
        self.assertEqual(out[:4], struct.pack("<I", 4))
        self.assertEqual(out[4:8], "S\u00e9\x00".encode("utf-8"))

    def test_none(self):
        # None encodes the same as an empty string (length = 0).
        out = wl_poke.pack_string(None)
        self.assertEqual(out, struct.pack("<I", 0))

    def test_round_trip(self):
        for s in ["", "a", "ab", "abcd", "hello world", "S\u00e9", "\u00ff\u0100"]:
            encoded = wl_poke.pack_string(s)
            decoded, off = wl_poke.unpack_string(encoded, 0)
            self.assertEqual(decoded, s, f"mismatch for {s!r}")
            self.assertEqual(off, len(encoded), f"offset for {s!r}")

    def test_unpack_zero_length(self):
        # A wire string with length=0 decodes to "" and consumes 4 bytes.
        out, off = wl_poke.unpack_string(struct.pack("<I", 0), 0)
        self.assertEqual(out, "")
        self.assertEqual(off, 4)


class ArrayTests(unittest.TestCase):
    def test_empty(self):
        out = wl_poke.pack_array_uint32([])
        self.assertEqual(out, struct.pack("<I", 0))

    def test_round_trip(self):
        values = [0, 1, 0xFFFFFFFF, 42]
        encoded = wl_poke.pack_array_uint32(values)
        decoded, off = wl_poke.unpack_array_uint32(encoded, 0)
        self.assertEqual(decoded, values)
        self.assertEqual(off, len(encoded))


class FixedTests(unittest.TestCase):
    def test_zero(self):
        self.assertEqual(wl_poke.wl_fixed_from_int(0), 0)

    def test_positive(self):
        # 1 -> 0x100, 256 -> 0x10000.
        self.assertEqual(wl_poke.wl_fixed_from_int(1), 0x100)
        self.assertEqual(wl_poke.wl_fixed_from_int(256), 0x10000)

    def test_negative(self):
        # -1 -> 0xFFFFFF00 on the wire (two's complement 32-bit).
        self.assertEqual(wl_poke.wl_fixed_from_int(-1), 0xFFFFFF00)

    def test_round_trip(self):
        for v in [-1024, -1, 0, 1, 7, 42, 256, 65535]:
            wire = wl_poke.wl_fixed_from_int(v)
            self.assertAlmostEqual(wl_poke.wl_fixed_to_int(wire), float(v), places=5)


class HeaderTests(unittest.TestCase):
    def test_pack_unpack(self):
        msg = wl_poke.pack_header(0x12345678, 5, 16)
        self.assertEqual(len(msg), 8)
        oid, word = struct.unpack("<II", msg)
        self.assertEqual(oid, 0x12345678)
        self.assertEqual(word, (16 << 16) | 5)
        # unpack_header is the inverse.
        o, size, op = wl_poke.unpack_header(msg)
        self.assertEqual(o, 0x12345678)
        self.assertEqual(size, 16)
        self.assertEqual(op, 5)

    def test_encode_request(self):
        # wl_display.get_registry (opcode 1) on id 1, payload = u32(new_id=2).
        msg = wl_poke.encode_request(1, 1, struct.pack("<I", 2))
        # Header (8 bytes) + 4-byte payload.
        self.assertEqual(len(msg), 12)
        o, word = struct.unpack("<II", msg[:8])
        self.assertEqual(o, 1)
        self.assertEqual(word, (12 << 16) | 1)
        self.assertEqual(msg[8:], struct.pack("<I", 2))

    def test_header_rejects_short_size(self):
        with self.assertRaises(ValueError):
            wl_poke.pack_header(1, 0, 4)

    def test_header_rejects_bad_opcode(self):
        with self.assertRaises(ValueError):
            wl_poke.pack_header(1, 0x10000, 8)

    def test_event_decoding(self):
        # Decode an event header from a buffer and slice its payload. This
        # mirrors what Connection._drain_one does internally.
        payload = struct.pack("<II", 0xABCD, 42)  # two globals
        header = wl_poke.pack_header(7, 3, 8 + len(payload))
        buf = header + payload
        oid, size, op = wl_poke.unpack_header(buf[:8])
        self.assertEqual(oid, 7)
        self.assertEqual(size, 8 + len(payload))
        self.assertEqual(op, 3)
        self.assertEqual(buf[8:size], payload)


# ---------------------------------------------------------------------------
# Connection end-to-end against a fake server.
# ---------------------------------------------------------------------------


class FakeServer:
    """Tiny Wayland-compatible server for testing Connection end-to-end.

    Listens on a socket, speaks just enough of the wire protocol to answer
    ``wl_display.get_registry`` with a couple of globals and ``wl_display
    .sync`` with a done event on a fresh callback. It does NOT validate
    requests; its job is to be a stable, predictable peer.
    """

    def __init__(self) -> None:
        # Two fds so each side can read + write without blocking on the
        # other end's read.
        self._a, self._b = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
        self._a.settimeout(2.0)
        self._b.settimeout(2.0)
        self._next_id = 3  # wl_display=1, wl_registry=2 already used by client
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._serve, daemon=True)

    @property
    def path(self) -> str:
        # socketpair() returns connected AF_UNIX sockets; the "path" we
        # hand to Connection is a marker that the connection.py side patches
        # out (we pass the file descriptor directly via _sock below).
        return "<socketpair>"

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        try:
            self._b.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self._b.close()
        self._thread.join(timeout=2.0)

    # The fake server only needs to send these canned events; the request
    # opcodes it recognises are listed in COMMENTS for clarity.
    def _serve(self) -> None:
        sock = self._a
        buf = bytearray()
        try:
            while not self._stop.is_set():
                # Read a full request.
                while len(buf) < 8:
                    try:
                        chunk = sock.recv(4096)
                    except TimeoutError:
                        continue
                    if not chunk:
                        return
                    buf.extend(chunk)
                oid, word = struct.unpack("<II", bytes(buf[:8]))
                size = word >> 16
                opcode = word & 0xFFFF
                while len(buf) < size:
                    try:
                        chunk = sock.recv(4096)
                    except TimeoutError:
                        continue
                    if not chunk:
                        return
                    buf.extend(chunk)
                # Snapshot the payload slice BEFORE consuming so we can
                # parse the u32 argument out of it.
                payload = bytes(buf[8:size])
                del buf[:size]
                # Dispatch by (oid, opcode).
                if oid == 1 and opcode == 1:
                    # get_registry: respond with two globals.
                    self._send_global(1, "wl_compositor", 4)
                    self._send_global(2, "wl_shm", 1)
                elif oid == 1 and opcode == 0:
                    # sync: payload is u32(new_id). Acknowledge with a
                    # done event on the requested callback id after a
                    # short delay so the client can exercise its dispatch
                    # loop.
                    (new_id,) = struct.unpack("<I", payload[:4])
                    time.sleep(0.01)
                    self._send_done(new_id)
        except (ConnectionError, OSError):
            return

    def _send_global(self, name: int, interface: str, version: int) -> None:
        payload = (
            struct.pack("<I", name)
            + wl_poke.pack_string(interface)
            + struct.pack("<I", version)
        )
        # wl_registry id=2, event=0 (global).
        msg = wl_poke.encode_request(2, 0, payload)
        self._a.sendall(msg)

    def _send_done(self, cb_id: int) -> None:
        # wl_callback id=cb_id, event=0 (done, serial=u32).
        payload = struct.pack("<I", cb_id)
        msg = wl_poke.encode_request(cb_id, 0, payload)
        self._a.sendall(msg)


class ConnectionEndToEndTests(unittest.TestCase):
    """Exercise ``Connection`` against the in-process FakeServer."""

    def _make_connection(self, server: FakeServer) -> wl_poke.Connection:
        conn = wl_poke.Connection(server.path, verbose=False)
        # Swap in the FakeServer's other half as the connected socket.
        # Connection.connect() created a new socket; undo it and use ours.
        conn.close()
        sock = server._b  # type: ignore[attr-defined]
        sock.settimeout(5.0)
        # Inject the pre-connected socket.
        conn._sock = sock  # type: ignore[attr-defined]
        # Recreate state as if we had just connected.
        conn._recv_buf = bytearray()
        conn._next_id = wl_poke.WL_REGISTRY_ID + 1
        conn.registry_id = wl_poke.WL_REGISTRY_ID
        return conn

    def test_registry_roundtrip(self):
        server = FakeServer()
        server.start()
        try:
            conn = self._make_connection(server)
            try:
                # Manually replay wl_display.get_registry since we skipped
                # connect()'s side effect of sending the request.
                conn.send(wl_poke.WL_DISPLAY_ID, 1, struct.pack("<I", conn.registry_id))
                conn.install_default_registry()
                conn.roundtrip()
                # Two globals must have arrived.
                interfaces = sorted(g["interface"] for g in conn.globals)
                self.assertEqual(interfaces, ["wl_compositor", "wl_shm"])
                self.assertEqual(conn.globals[0]["name"], 1)
                self.assertEqual(conn.globals[1]["name"], 2)
            finally:
                conn.close()
        finally:
            server.stop()

    def test_sync_roundtrip(self):
        server = FakeServer()
        server.start()
        try:
            conn = self._make_connection(server)
            try:
                conn.send(wl_poke.WL_DISPLAY_ID, 1, struct.pack("<I", conn.registry_id))
                conn.install_default_registry()
                # The registry roundtrip itself relies on sync working;
                # we already exercised that in test_registry_roundtrip.
                # Now do an explicit sync to make sure nothing hangs.
                cb_id = conn.alloc_id()
                conn.send(wl_poke.WL_DISPLAY_ID, 0, struct.pack("<I", cb_id))
                # Register a handler the same way Connection.roundtrip does.
                done = {"fired": False}

                def handler(opc: int, _data: bytes) -> None:
                    if opc == 0:
                        done["fired"] = True

                conn.on_event(cb_id, handler)
                deadline = time.monotonic() + 5.0
                while not done["fired"] and time.monotonic() < deadline:
                    conn.dispatch(max_events=64)
                self.assertTrue(done["fired"], "sync callback never fired")
            finally:
                conn.close()
        finally:
            server.stop()


# ---------------------------------------------------------------------------
# Error path tests (no fake server needed).
# ---------------------------------------------------------------------------


class ErrorPathTests(unittest.TestCase):
    def test_display_error_decoded(self):
        # Build a synthetic wl_display.error event: object_id(u32) +
        # code(u32) + message(string). The Connection should raise
        # WaylandError when it dispatches this.
        payload = (
            struct.pack("<I", 0x42) + struct.pack("<I", 7) + wl_poke.pack_string("boom")
        )
        conn = wl_poke.Connection("/nonexistent")
        # Bypass connect(); install a fake sock to feed the message.
        import socket as _socket

        a, b = _socket.socketpair(_socket.AF_UNIX, _socket.SOCK_STREAM)
        conn._sock = a  # type: ignore[attr-defined]
        conn._sock.settimeout(1.0)  # type: ignore[attr-defined]
        # Send the error event from the other side.
        b.sendall(wl_poke.encode_request(wl_poke.WL_DISPLAY_ID, 0, payload))
        b.close()
        with self.assertRaises(wl_poke.WaylandError) as cm:
            conn.dispatch(max_events=4)
        self.assertIn("boom", str(cm.exception))


# ---------------------------------------------------------------------------
# Subcommand wiring tests (do not actually open a real socket).
# ---------------------------------------------------------------------------


class SubcommandTests(unittest.TestCase):
    def test_help_for_each_subcommand(self):
        # argparse should not raise SystemExit for -h; we capture it.
        import io
        from contextlib import redirect_stderr, redirect_stdout

        for cmd in ("globals", "motion", "key", "inhibit"):
            buf = io.StringIO()
            with redirect_stdout(buf), redirect_stderr(buf):
                try:
                    rc = wl_poke.main([cmd, "--help"])
                except SystemExit as e:
                    rc = e.code
            self.assertIn(rc, (0, None))

    def test_missing_global_returns_none(self):
        # The helper returns None for an interface that isn't there and a
        # tuple when it is. No socket access required.
        conn = wl_poke.Connection("/nonexistent")
        conn.globals = [{"name": 1, "interface": "wl_seat", "version": 7}]
        self.assertEqual(
            wl_poke._find_global(conn, "wl_seat", 1),
            (1, 7),
        )
        self.assertIsNone(wl_poke._find_global(conn, "nope", 1))

    def test_find_socket_path_defaults(self):
        # With both env vars unset, the path must be /run/user/<uid>/wayland-0.
        saved_wd = os.environ.pop("WAYLAND_DISPLAY", None)
        saved_rt = os.environ.pop("XDG_RUNTIME_DIR", None)
        try:
            path = wl_poke.find_socket_path()
            self.assertTrue(path.endswith("wayland-0"))
            self.assertIn(f"/run/user/{os.getuid()}", path)
        finally:
            if saved_wd is not None:
                os.environ["WAYLAND_DISPLAY"] = saved_wd
            if saved_rt is not None:
                os.environ["XDG_RUNTIME_DIR"] = saved_rt


if __name__ == "__main__":
    unittest.main()
