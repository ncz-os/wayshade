#!/usr/bin/python3
# SPDX-License-Identifier: Apache-2.0
"""wl_poke - raw-wire Wayland client for the NCZ-OS screensaver test harness.

The compositor on the build hosts (labwc / wlroots 0.20) advertises a small
set of unstable protocols that the harness uses to inject input and hold an
idle inhibitor inside the user's session. The full protocol tree is huge and
unstable so this tool does not try to be a general Wayland binding: it speaks
just the wire frames we need, with no pywayland/ctypes-libwayland and no
third-party Python modules (Python 3.13 stdlib only).

Wire format (see Wayland spec section "Wire format"):
  * every request and event begins with a 32-bit destination/source object id,
    followed by a 32-bit word carrying ``(size << 16) | opcode`` (size is the
    total message length in bytes, including the header);
  * arguments are packed in native byte order, each padded up to a 4-byte
    multiple;
  * ``string`` is u32 length (including the trailing NUL) followed by the
    UTF-8 bytes plus padding to a 4-byte boundary;
  * ``fd`` is sent out-of-band via ``SCM_RIGHTS``; fds received in a reply are
    delivered via ``recvmsg`` as ancillary data.

Subcommands:
  motion    -- inject relative pointer motion via zwlr_virtual_pointer_v1
  key       -- inject a key event via zwp_virtual_keyboard_v1
  inhibit   -- hold a zwp_idle_inhibitor_v1 for N seconds on a mapped xdg
               toplevel (labwc only honours an inhibitor while the surface
               is visible)
  globals   -- dump wl_registry globals as JSON

Run ``wl_poke --help`` for per-subcommand flags. ``--verbose`` prints a
human-readable protocol trace to stderr.
"""

from __future__ import annotations

import argparse
import array
import json
import os
import socket
import struct
import sys
import time
from collections.abc import Callable, Iterable
from typing import Any

# Native byte order -- Wayland is endian-sensitive. x86_64 and aarch64 (the
# only build targets) are both little-endian, but we stay explicit so the
# code is auditable.
_HEADER = struct.Struct("II")  # object id, (size<<16)|opcode
_U32 = struct.Struct("I")
_U32x2 = struct.Struct("II")
_I32 = struct.Struct("i")


# ---------------------------------------------------------------------------
# Wire helpers (no compositor access).
# ---------------------------------------------------------------------------


def pad4(n: int) -> int:
    """Round ``n`` up to the next multiple of 4. Zero is a multiple of 4."""
    return (n + 3) & ~3


def pack_string(value: str | None) -> bytes:
    """Pack a Wayland ``string`` argument.

    The wire form is: ``u32 length`` (including the trailing NUL) followed
    by the UTF-8 bytes plus padding to a 4-byte boundary. The Wayland spec
    says an empty string is encoded as ``length = 0`` with no payload and
    no padding -- libwayland reads that as the empty string. ``None``
    (a null pointer in the C API) is encoded as length 0 as well, since
    every place we pass a string here it is genuinely required and ``None``
    would be a programming error.
    """
    if not value:
        # Empty string AND None both encode as length = 0. The decoder
        # treats both as "" (libwayland does the same).
        return _U32.pack(0)
    payload = value.encode("utf-8") + b"\x00"
    length = len(payload)
    out = _U32.pack(length) + payload
    pad = pad4(length) - length
    if pad:
        out += b"\x00" * pad
    return out


def unpack_string(buf: bytes, idx: int) -> tuple[str, int]:
    """Unpack a ``string`` from a bytes buffer starting at ``idx``.

    Returns ``(value, new_idx)``. The new index points just past the trailing
    padding. A zero-length string and a string consisting of a single NUL
    byte both decode to the empty string, matching libwayland's contract.
    """
    (length,) = _U32.unpack_from(buf, idx)
    idx += 4
    if length == 0:
        return "", idx
    raw = buf[idx : idx + length]
    # strip exactly one trailing NUL if present, then decode
    if raw.endswith(b"\x00"):
        raw = raw[:-1]
    value = raw.decode("utf-8", errors="replace")
    idx += pad4(length)
    return value, idx


def pack_array_uint32(values: Iterable[int]) -> bytes:
    """Pack a Wayland ``array`` argument (u32 length + u32 elements)."""
    values = list(values)
    out = _U32.pack(len(values)) + b"".join(_U32.pack(v & 0xFFFFFFFF) for v in values)
    return out


def unpack_array_uint32(buf: bytes, idx: int) -> tuple[list[int], int]:
    """Unpack a u32 ``array`` argument. Returns ``(values, new_idx)``."""
    (length,) = _U32.unpack_from(buf, idx)
    idx += 4
    out: list[int] = []
    for _ in range(length):
        (v,) = _U32.unpack_from(buf, idx)
        out.append(v)
        idx += 4
    return out, idx


def wl_fixed_from_int(value: int) -> int:
    """Convert a Python int to a 24.8 fixed-point wire value.

    The fixed-point representation has 24 bits above the binary point
    (signed) and 8 bits below, giving a range of roughly +/-8.3 million
    with a step of 1/256. The wire form is just the bits; conversion is a
    left shift by 8, masked to 32 bits so a signed input becomes the
    expected unsigned wire word.
    """
    return (value << 8) & 0xFFFFFFFF


def wl_fixed_to_int(value: int) -> float:
    """Convert a 24.8 fixed-point wire value to a Python float."""
    if value & 0x80000000:
        value -= 0x100000000
    return value / 256.0


def pack_header(object_id: int, opcode: int, size: int) -> bytes:
    """Pack the 8-byte Wayland message header.

    ``size`` is the total message length in bytes, including the header
    itself. The header word is ``(size << 16) | (opcode & 0xFFFF)``.
    """
    if size < 8:
        raise ValueError("Wayland messages are at least 8 bytes")
    if opcode < 0 or opcode > 0xFFFF:
        raise ValueError(f"opcode out of range: {opcode}")
    return _HEADER.pack(
        object_id & 0xFFFFFFFF, ((size & 0xFFFF) << 16) | (opcode & 0xFFFF)
    )


def unpack_header(buf: bytes) -> tuple[int, int, int]:
    """Return ``(object_id, size, opcode)`` from a 8-byte header."""
    (object_id, word) = _HEADER.unpack(buf[:8])
    size = word >> 16
    opcode = word & 0xFFFF
    return object_id, size, opcode


def encode_request(object_id: int, opcode: int, payload: bytes = b"") -> bytes:
    """Build a complete request message (header + payload) ready for send."""
    size = _HEADER.size + len(payload)
    return pack_header(object_id, opcode, size) + payload


# ---------------------------------------------------------------------------
# Connection: AF_UNIX wire transport + tiny dispatch table.
# ---------------------------------------------------------------------------


# Display id used for ``wl_display`` requests. libwayland uses 1; the protocol
# reserves 0 for the client itself, so 1 is the natural id.
WL_DISPLAY_ID = 1
# Registry id used for wl_registry (we always allocate it at 2).
WL_REGISTRY_ID = 2

# Default per-recv timeout. The compositor can take its time answering, but
# the harness would rather fail loudly than hang forever.
SOCKET_TIMEOUT = 5.0


class WaylandError(RuntimeError):
    """Raised when the compositor sends a fatal ``wl_display.error`` event."""


class Connection:
    """Raw-wire Wayland client connection over an AF_UNIX socket.

    The connection holds:
      * the connected socket (5 s recv timeout);
      * the next free client-allocated object id;
      * a registry cache mapping ``(name, interface, version) -> bound id``;
      * a tiny per-object dispatch table driven by closure callbacks
        installed with ``on_event``.

    The connection speaks the wire format directly, no libwayland, no ctypes.
    """

    def __init__(self, socket_path: str, verbose: bool = False) -> None:
        self._path = socket_path
        self._verbose = verbose
        self._next_id = WL_REGISTRY_ID + 1
        self._sock: socket.socket | None = None
        self._recv_buf = bytearray()
        # Per-object-id dispatch (event signature depends on the interface).
        self._event_handlers: dict[int, Callable[..., None]] = {}
        # Registry state.
        self.registry_id: int = WL_REGISTRY_ID
        self.globals: list[dict[str, Any]] = []
        self._bound_ids: dict[tuple[int, str], int] = {}

    # ------------------------------------------------------------------
    # Socket lifecycle.
    # ------------------------------------------------------------------

    def connect(self) -> None:
        """Open the AF_UNIX socket and bind the wl_registry at id 2."""
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM | socket.SOCK_CLOEXEC)
        sock.settimeout(SOCKET_TIMEOUT)
        sock.connect(self._path)
        self._sock = sock
        self._trace("connect", WL_DISPLAY_ID, 0)
        # wl_display.get_registry(new_id) opcode = 1; bind id 2 eagerly.
        self.registry_id = WL_REGISTRY_ID
        payload = _U32.pack(self.registry_id & 0xFFFFFFFF)
        self._send(WL_DISPLAY_ID, 1, payload, fds=())

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            finally:
                self._sock = None

    # ------------------------------------------------------------------
    # Allocating / looking up client object ids.
    # ------------------------------------------------------------------

    def alloc_id(self) -> int:
        """Allocate a fresh client-side object id.

        The first two ids are reserved (wl_display=1, wl_registry=2). Beyond
        that the client picks anything > 0; we just bump a counter.
        """
        nid = self._next_id
        self._next_id += 1
        return nid

    def bind(self, name: int, interface: str, version: int) -> int:
        """``wl_registry.bind`` -- allocate a new id and ask the server to
        instantiate the named global on it.

        Returns the new client-side id. Eagerly records the
        ``(name, interface) -> bound id`` mapping for our own bookkeeping.
        """
        new_id = self.alloc_id()
        payload = (
            _U32.pack(name & 0xFFFFFFFF)
            + pack_string(interface)
            + _U32.pack(version & 0xFFFFFFFF)
            + _U32.pack(new_id & 0xFFFFFFFF)
        )
        self._send(self.registry_id, 0, payload, fds=())  # wl_registry.bind
        self._bound_ids[(name, interface)] = new_id
        return new_id

    # ------------------------------------------------------------------
    # Sending.
    # ------------------------------------------------------------------

    def _send(
        self,
        object_id: int,
        opcode: int,
        payload: bytes,
        fds: tuple[int, ...] = (),
    ) -> None:
        """Build a complete message (header + payload) and ship it.

        ``fds`` is delivered via ``SCM_RIGHTS`` ancillary data. The wire
        body of an fd-typed argument does NOT include the fd; only the
        surrounding scalar fields do.
        """
        if self._sock is None:
            raise RuntimeError("Connection.send called before connect()")
        msg = encode_request(object_id, opcode, payload)
        if fds:
            self._sock.sendmsg(
                [msg],
                [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", fds))],
            )
            self._trace("sendmsg", object_id, opcode, fds=list(fds))
        else:
            self._sock.sendall(msg)
            self._trace("send", object_id, opcode)
        self._sock.settimeout(SOCKET_TIMEOUT)  # reset on activity

    def send(
        self,
        object_id: int,
        opcode: int,
        payload: bytes = b"",
        fds: tuple[int, ...] = (),
    ) -> None:
        """Public send wrapper (used by subcommands)."""
        self._send(object_id, opcode, payload, fds)

    # ------------------------------------------------------------------
    # Receiving and dispatch.
    # ------------------------------------------------------------------

    def _recv_more(self, need: int) -> None:
        """Pull more bytes from the socket until ``need`` are buffered.

        Raises ``socket.timeout`` after ``SOCKET_TIMEOUT`` if the compositor
        is silent. Raises ``ConnectionError`` on EOF.
        """
        assert self._sock is not None
        while len(self._recv_buf) < need:
            chunk = self._sock.recv(65536)
            if not chunk:
                raise ConnectionError("Wayland socket closed by peer")
            self._recv_buf.extend(chunk)

    def _consume(self, n: int) -> None:
        del self._recv_buf[:n]

    def _drain_one(self) -> tuple[int, int, bytes] | None:
        """Return ``(object_id, opcode, payload)`` of the next message.

        Returns ``None`` on timeout (5 s) -- this is the "no message right
        now" signal used by ``roundtrip`` and the inhibit event loop.
        """
        assert self._sock is not None
        try:
            self._recv_more(8)
        except TimeoutError:
            return None
        object_id, size, opcode = unpack_header(bytes(self._recv_buf[:8]))
        try:
            self._recv_more(size)
        except TimeoutError:
            return None
        payload = bytes(self._recv_buf[8:size])
        self._consume(size)
        self._trace("recv", object_id, opcode)
        return object_id, opcode, payload

    def _dispatch(self, object_id: int, opcode: int, payload: bytes) -> None:
        """Call the per-object handler for ``(object_id, opcode)``.

        ``wl_display.error`` (object_id == 1, opcode 0) is fatal and raises
        immediately -- the compositor is shutting us down anyway.
        """
        if object_id == WL_DISPLAY_ID and opcode == 0:
            # wl_display.error: object_id(u32), code(u32), message(string)
            err_object = _U32.unpack(payload[:4])[0]
            code = _U32.unpack(payload[4:8])[0]
            msg = unpack_string(payload, 8)[0]
            raise WaylandError(
                f"wl_display.error on {err_object:#x} code={code}: {msg}"
            )
        handler = self._event_handlers.get(object_id)
        if handler is not None:
            handler(opcode, payload)
        # If no handler is installed we simply drop the event. Many events
        # (wl_buffer.release, wl_shm.format, ...) are not interesting here.

    def dispatch(self, max_events: int = 64) -> int:
        """Read and dispatch up to ``max_events`` messages. Returns the
        number actually dispatched; ``0`` means the compositor was silent
        for the full 5 s timeout.
        """
        n = 0
        for _ in range(max_events):
            msg = self._drain_one()
            if msg is None:
                break
            object_id, opcode, payload = msg
            self._dispatch(object_id, opcode, payload)
            n += 1
        return n

    def roundtrip(self) -> None:
        """``wl_display.sync`` + wl_callback.done barrier.

        Sends a sync request, then drains messages until the matching
        wl_callback.done arrives (or a 5 s timeout elapses). All events
        queued before the done are dispatched, which is what makes this
        useful for "wait until the compositor has acknowledged everything
        we sent so far".
        """
        cb_id = self.alloc_id()
        payload = _U32.pack(cb_id & 0xFFFFFFFF)
        done = {"fired": False, "serial": 0}

        def handler(opcode: int, data: bytes) -> None:
            # wl_callback has exactly one event: opcode 0 (done, with serial).
            if opcode == 0:
                (serial,) = _U32.unpack(data[:4])
                done["fired"] = True
                done["serial"] = serial

        self._event_handlers[cb_id] = handler
        self._send(WL_DISPLAY_ID, 0, payload)  # wl_display.sync
        deadline = time.monotonic() + SOCKET_TIMEOUT
        while not done["fired"]:
            # One message at a time: dispatch(64) would block on the socket
            # timeout after the done event when the compositor goes quiet.
            msg = self._drain_one()
            if msg is not None:
                self._dispatch(*msg)
            if time.monotonic() > deadline:
                raise TimeoutError("wl_display.roundtrip timed out after 5 s")
        self._event_handlers.pop(cb_id, None)

    # ------------------------------------------------------------------
    # Registry plumbing.
    # ------------------------------------------------------------------

    def install_default_registry(self) -> None:
        """Install a handler that records every ``wl_registry.global`` event
        in ``self.globals``. Idempotent. Call *before* ``roundtrip()`` so the
        first batch of global events is captured.
        """
        if self._event_handlers.get(self.registry_id) is not self._registry_event:
            self._event_handlers[self.registry_id] = self._registry_event

    def _registry_event(self, opcode: int, payload: bytes) -> None:
        if opcode == 0:
            # global: name(u32), interface(string), version(u32)
            name = _U32.unpack(payload[:4])[0]
            iface, off = unpack_string(payload, 4)
            version = _U32.unpack(payload[off : off + 4])[0]
            self.globals.append({"name": name, "interface": iface, "version": version})
        elif opcode == 1:
            # global_remove: name(u32). Nothing for us to do.
            pass

    # ------------------------------------------------------------------
    # Per-object event hook.
    # ------------------------------------------------------------------

    def on_event(self, object_id: int, handler: Callable[..., None]) -> None:
        """Install an event handler for the given client-side object id.

        The handler signature is ``(opcode: int, payload: bytes)``. A
        handler installed on the same id overwrites any earlier one.
        """
        self._event_handlers[object_id] = handler

    # ------------------------------------------------------------------
    # Debug trace.
    # ------------------------------------------------------------------

    def _trace(self, kind: str, object_id: int, opcode: int, **kw: Any) -> None:
        if not self._verbose:
            return
        extra = " ".join(f"{k}={v}" for k, v in kw.items())
        sys.stderr.write(
            f"[wl_poke] {kind} object={object_id:#x} opcode={opcode}"
            f"{(' ' + extra) if extra else ''}\n"
        )


# ---------------------------------------------------------------------------
# Wayland socket discovery.
# ---------------------------------------------------------------------------


def find_socket_path(display: str | None = None, runtime: str | None = None) -> str:
    """Return the AF_UNIX path of the compositor socket.

    Honours ``$WAYLAND_DISPLAY`` and ``$XDG_RUNTIME_DIR``; defaults to
    ``/run/user/$UID/wayland-0`` which is where greetd + labwc put the socket
    on the build hosts.
    """
    display = display or os.environ.get("WAYLAND_DISPLAY") or "wayland-0"
    runtime = runtime or os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}"
    return os.path.join(runtime, display)


# ---------------------------------------------------------------------------
# Minimal XKB keymap for the virtual keyboard.
# ---------------------------------------------------------------------------


# A tiny but well-formed xkb_keymap. Compositors load this via mmap on the
# fd we hand them; if the file isn't a valid keymap the compositor raises
# ``no_keymap`` and the virtual keyboard is unusable. The trailing NUL is
# included in the size we send, per the xkb spec (keymaps are NUL-terminated
# text).
XKB_KEYMAP_TEXT = (
    "xkb_keymap {\n"
    ' xkb_keycodes { include "evdev+aliases(qwerty)" };\n'
    ' xkb_types { include "complete" };\n'
    ' xkb_compat { include "complete" };\n'
    ' xkb_symbols { include "pc+us" };\n'
    "};\n"
    "\x00"
)


def keymap_size(text: str = XKB_KEYMAP_TEXT) -> int:
    """Number of bytes the keymap occupies on the wire (NUL included)."""
    return len(text.encode("utf-8"))


def keymap_memfd(text: str = XKB_KEYMAP_TEXT) -> int:
    """Write the keymap into a sealed memfd and return its fd.

    We ``ftruncate`` to the encoded byte length (including the trailing NUL)
    and ``lseek`` back to 0 so the compositor can ``mmap`` from the start.
    """
    data = text.encode("utf-8")
    fd = os.memfd_create("wl_poke-keymap", 0)
    try:
        os.write(fd, data)
        os.ftruncate(fd, len(data))
        os.lseek(fd, 0, os.SEEK_SET)
    except BaseException:
        os.close(fd)
        raise
    return fd


# ---------------------------------------------------------------------------
# Registry helpers.
# ---------------------------------------------------------------------------


def _find_global(
    conn: Connection, interface: str, min_version: int
) -> tuple[int, int] | None:
    """Return ``(name, version)`` for the first matching global or ``None``."""
    for g in conn.globals:
        if g["interface"] == interface and g["version"] >= min_version:
            return g["name"], g["version"]
    return None


def _require_global(conn: Connection, interface: str) -> int:
    """Bind to ``interface`` (v1) and return the new id, or exit 1."""
    found = _find_global(conn, interface, 1)
    if found is None:
        sys.stderr.write(f"wl_poke: compositor does not advertise {interface}\n")
        sys.exit(1)
    name, version = found
    return conn.bind(name, interface, min(version, 1))


# ---------------------------------------------------------------------------
# Subcommand: globals
# ---------------------------------------------------------------------------


def cmd_globals(args: argparse.Namespace) -> int:
    """Print wl_registry globals as JSON. Used to record which protocols a
    given compositor advertises (the design document uses this to verify
    that labwc exposes zwlr_virtual_pointer_manager_v1 et al.).
    """
    conn = Connection(find_socket_path(), verbose=args.verbose)
    try:
        conn.connect()
        # We need to install the registry handler *before* the global events
        # arrive; the first batch is dispatched during roundtrip().
        conn.install_default_registry()
        conn.roundtrip()
    finally:
        conn.close()
    json.dump(conn.globals, sys.stdout, indent=2)
    sys.stdout.write("\n")
    sys.stdout.flush()
    return 0


# ---------------------------------------------------------------------------
# Subcommand: motion (zwlr_virtual_pointer_v1)
# ---------------------------------------------------------------------------


def _bind_virtual_pointer(conn: Connection) -> tuple[int, int, int]:
    """Bind zwlr_virtual_pointer_manager_v1 + a wl_seat, create a pointer.

    Returns ``(seat_id, pointer_manager_id, pointer_id)``. Exits 1 if the
    compositor lacks either global.
    """
    seat_global = _find_global(conn, "wl_seat", 1)
    if seat_global is None:
        sys.stderr.write("wl_poke: compositor does not advertise wl_seat\n")
        sys.exit(1)
    seat_name, _ = seat_global
    seat_id = conn.bind(seat_name, "wl_seat", 1)

    pointer_mgr_global = _find_global(conn, "zwlr_virtual_pointer_manager_v1", 1)
    if pointer_mgr_global is None:
        sys.stderr.write(
            "wl_poke: compositor does not advertise zwlr_virtual_pointer_manager_v1\n"
        )
        sys.exit(1)
    name, version = pointer_mgr_global
    # We bind at min(2, advertised) -- v2 unlocks motion_absolute but v1 is
    # enough for the relative motion we actually use.
    bind_version = min(2, version)
    mgr_id = conn.bind(name, "zwlr_virtual_pointer_manager_v1", bind_version)

    pointer_id = conn.alloc_id()
    # create_virtual_pointer(seat: object, new_id) opcode = 0
    payload = _U32.pack(seat_id & 0xFFFFFFFF) + _U32.pack(pointer_id & 0xFFFFFFFF)
    conn.send(mgr_id, 0, payload)
    return seat_id, mgr_id, pointer_id


def cmd_click(args: argparse.Namespace) -> int:
    """Move the virtual pointer to (x, y) in a width x height extent and click."""
    conn = Connection(find_socket_path(), verbose=args.verbose)
    try:
        conn.connect()
        conn.install_default_registry()
        conn.roundtrip()
        _seat_id, mgr_id, pointer_id = _bind_virtual_pointer(conn)
        try:
            now_ms = int(time.time() * 1000)
            # motion_absolute(time, x, y, x_extent, y_extent) opcode 1
            payload = b"".join(
                _U32.pack(v & 0xFFFFFFFF)
                for v in (now_ms, args.x, args.y, args.width, args.height)
            )
            conn.send(pointer_id, 1, payload)
            conn.send(pointer_id, 4, b"")
            time.sleep(0.15)
            for state in (1, 0):
                now_ms += 40
                # button(time, button, state) opcode 2; BTN_LEFT is 0x110
                conn.send(
                    pointer_id,
                    2,
                    _U32.pack(now_ms & 0xFFFFFFFF)
                    + _U32.pack(args.button)
                    + _U32.pack(state),
                )
                conn.send(pointer_id, 4, b"")
                time.sleep(0.08)
            conn.roundtrip()
        finally:
            conn.send(pointer_id, 8, b"")
            if mgr_id:
                conn.send(mgr_id, 2, b"")
    finally:
        conn.close()
    return 0


def cmd_scroll(args: argparse.Namespace) -> int:
    """Move to (x, y) and scroll vertically by `amount` (positive scrolls down)."""
    conn = Connection(find_socket_path(), verbose=args.verbose)
    try:
        conn.connect()
        conn.install_default_registry()
        conn.roundtrip()
        _seat_id, mgr_id, pointer_id = _bind_virtual_pointer(conn)
        try:
            now_ms = int(time.time() * 1000)
            payload = b"".join(
                _U32.pack(v & 0xFFFFFFFF)
                for v in (now_ms, args.x, args.y, args.width, args.height)
            )
            conn.send(pointer_id, 1, payload)
            conn.send(pointer_id, 4, b"")
            time.sleep(0.2)
            for _ in range(args.steps):
                now_ms += 30
                # axis_source(wheel) opcode 5, axis(time, axis, value) opcode 3
                conn.send(pointer_id, 5, _U32.pack(0))
                conn.send(
                    pointer_id,
                    3,
                    _U32.pack(now_ms & 0xFFFFFFFF)
                    + _U32.pack(0)
                    + _U32.pack(wl_fixed_from_int(args.amount)),
                )
                conn.send(pointer_id, 4, b"")
                time.sleep(0.08)
            conn.roundtrip()
        finally:
            conn.send(pointer_id, 8, b"")
            if mgr_id:
                conn.send(mgr_id, 2, b"")
    finally:
        conn.close()
    return 0


def cmd_motion(args: argparse.Namespace) -> int:
    """Bind zwlr_virtual_pointer_manager_v1 and a wl_seat, create a virtual
    pointer, send ``count`` relative motions, then tear it down. The
    defaults (dx=7, dy=3, count=3, interval=0.2) are large enough to trigger
    any reasonable idle-detection threshold on a real compositor and short
    enough that the harness stays snappy.
    """
    conn = Connection(find_socket_path(), verbose=args.verbose)
    try:
        conn.connect()
        conn.install_default_registry()
        conn.roundtrip()
        _seat_id, mgr_id, pointer_id = _bind_virtual_pointer(conn)
        try:
            now_ms = int(time.time() * 1000)
            for i in range(args.count):
                now_ms += int(args.interval * 1000)
                # zwlr_virtual_pointer_v1.motion(time:uint, dx:fixed, dy:fixed)
                # opcode = 0
                payload = (
                    _U32.pack(now_ms & 0xFFFFFFFF)
                    + _U32.pack(wl_fixed_from_int(args.dx))
                    + _U32.pack(wl_fixed_from_int(args.dy))
                )
                conn.send(pointer_id, 0, payload)
                # frame() opcode = 4
                conn.send(pointer_id, 4, b"")
                if args.verbose:
                    sys.stderr.write(
                        f"[wl_poke] motion dx={args.dx} dy={args.dy} i={i + 1}/{args.count}\n"
                    )
                if i + 1 < args.count:
                    time.sleep(args.interval)
            # Roundtrip so the compositor actually consumes our requests
            # before we destroy the pointer.
            conn.roundtrip()
        finally:
            # zwlr_virtual_pointer_v1.destroy opcode = 8
            conn.send(pointer_id, 8, b"")
            # zwlr_virtual_pointer_manager_v1.destroy opcode = 2 (since v1)
            if mgr_id:
                conn.send(mgr_id, 2, b"")
    finally:
        conn.close()
    return 0


# ---------------------------------------------------------------------------
# Subcommand: key (zwp_virtual_keyboard_v1)
# ---------------------------------------------------------------------------


def cmd_key(args: argparse.Namespace) -> int:
    """Bind zwp_virtual_keyboard_manager_v1 and a wl_seat, push a minimal XKB
    keymap via memfd, then send a press/release pair ``--count`` times. The
    default keycode (42) is KEY_LEFTSHIFT -- benign and always present in
    evdev.
    """
    conn = Connection(find_socket_path(), verbose=args.verbose)
    try:
        conn.connect()
        conn.install_default_registry()
        conn.roundtrip()
        seat_global = _find_global(conn, "wl_seat", 1)
        if seat_global is None:
            sys.stderr.write("wl_poke: compositor does not advertise wl_seat\n")
            sys.exit(1)
        seat_name, _ = seat_global
        seat_id = conn.bind(seat_name, "wl_seat", 1)

        kbd_mgr_global = _find_global(conn, "zwp_virtual_keyboard_manager_v1", 1)
        if kbd_mgr_global is None:
            sys.stderr.write(
                "wl_poke: compositor does not advertise zwp_virtual_keyboard_manager_v1\n"
            )
            sys.exit(1)
        kbd_mgr_name, _ = kbd_mgr_global
        mgr_id = conn.bind(kbd_mgr_name, "zwp_virtual_keyboard_manager_v1", 1)

        kbd_id = conn.alloc_id()
        # create_virtual_keyboard(seat: object, new_id) opcode = 0
        payload = _U32.pack(seat_id & 0xFFFFFFFF) + _U32.pack(kbd_id & 0xFFFFFFFF)
        conn.send(mgr_id, 0, payload)

        # Push the keymap via a memfd. The fd travels in SCM_RIGHTS, NOT in
        # the wire body, even though the protocol claims the field is "fd".
        kfd = keymap_memfd()
        try:
            # keymap(format:uint, fd, size:uint) opcode = 0. Only ``format``
            # and ``size`` go in the wire body; ``fd`` is ancillary.
            km_payload = _U32.pack(1) + _U32.pack(keymap_size())
            conn.send(kbd_id, 0, km_payload, fds=(kfd,))
        except BaseException:
            os.close(kfd)
            raise

        # Each key is a press then a release, 30 ms apart. --codes types a
        # whole sequence (used to unlock the lab hosts' test sessions).
        seq = (
            [int(c) for c in args.codes.split(",")]
            if args.codes
            else [args.code] * args.count
        )
        now_ms = int(time.time() * 1000)
        for code in seq:
            for state in (1, 0):
                now_ms += 30
                payload = (
                    _U32.pack(now_ms & 0xFFFFFFFF)
                    + _U32.pack(code & 0xFFFFFFFF)
                    + _U32.pack(state)
                )
                conn.send(kbd_id, 1, payload)  # key(time, key, state) opcode 1
                time.sleep(0.03)
        conn.roundtrip()
        # zwp_virtual_keyboard_v1.destroy opcode = 3 (last request in v1).
        conn.send(kbd_id, 3, b"")
    finally:
        conn.close()
    return 0


# ---------------------------------------------------------------------------
# Subcommand: inhibit (zwp_idle_inhibitor_v1 on a mapped xdg toplevel)
# ---------------------------------------------------------------------------


def cmd_inhibit(args: argparse.Namespace) -> int:
    """Hold a zwp_idle_inhibitor_v1 for ``--seconds``.

    We have to map an xdg toplevel with a real buffer first: labwc only
    honours an idle inhibitor while the surface is visible. The toplevel
    gets a tiny 64x64 dark-gray buffer at default position; we do not
    focus or decorate it -- it exists only to keep the inhibitor alive.

    The function prints "inhibitor active" to stdout (flushed) once the
    inhibitor object exists, so a shell harness can synchronize.
    """
    conn = Connection(find_socket_path(), verbose=args.verbose)
    try:
        conn.connect()
        conn.install_default_registry()
        conn.roundtrip()

        # Acquire the globals we need; fail fast on any that are missing.
        compositor_id = _require_global(conn, "wl_compositor")
        shm_id = _require_global(conn, "wl_shm")
        wm_id = _require_global(conn, "xdg_wm_base")
        inhibit_mgr_id = _require_global(conn, "zwp_idle_inhibit_manager_v1")

        surface_id = conn.alloc_id()
        # wl_compositor.create_surface(new_id) opcode = 0
        conn.send(compositor_id, 0, _U32.pack(surface_id & 0xFFFFFFFF))

        xdg_surface_id = conn.alloc_id()
        # xdg_wm_base.get_xdg_surface(new_id, surface) opcode = 2
        payload = _U32.pack(xdg_surface_id & 0xFFFFFFFF) + _U32.pack(
            surface_id & 0xFFFFFFFF
        )
        conn.send(wm_id, 2, payload)

        xdg_toplevel_id = conn.alloc_id()
        # xdg_surface.get_toplevel(new_id) opcode = 1
        conn.send(xdg_surface_id, 1, _U32.pack(xdg_toplevel_id & 0xFFFFFFFF))
        # xdg_toplevel.set_title(string) opcode = 2
        conn.send(xdg_toplevel_id, 2, pack_string("ncz-idle-inhibit-test"))
        # xdg_toplevel.set_app_id(string) opcode = 3
        conn.send(xdg_toplevel_id, 3, pack_string("ncz.idle-inhibit-test"))

        # Build a 64x64 opaque dark-gray shm buffer (premultiplied ARGB8888
        # so the compositor doesn't get spooked by a translucent surface).
        width = height = 64
        stride = width * 4
        buf_size = stride * height
        buf_fd = os.memfd_create("wl_poke-inhibit-buf", 0)
        try:
            os.write(buf_fd, b"\x20\x20\x20\xff" * (width * height))
            os.ftruncate(buf_fd, buf_size)
            os.lseek(buf_fd, 0, os.SEEK_SET)

            pool_id = conn.alloc_id()
            # wl_shm.create_pool(new_id, fd, size:uint) opcode = 0. fd is
            # ancillary, ``size`` goes in the wire body.
            conn.send(
                shm_id,
                0,
                _U32.pack(pool_id & 0xFFFFFFFF) + _U32.pack(buf_size),
                fds=(buf_fd,),
            )

            buffer_id = conn.alloc_id()
            # wl_shm_pool.create_buffer(new_id, offset:int, width, height,
            # stride, format:uint) opcode = 0; ARGB8888 = 1.
            buffer_payload = (
                _U32.pack(buffer_id & 0xFFFFFFFF)
                + _I32.pack(0)  # offset
                + _U32.pack(width & 0xFFFFFFFF)
                + _U32.pack(height & 0xFFFFFFFF)
                + _U32.pack(stride & 0xFFFFFFFF)
                + _U32.pack(1)  # WL_SHM_FORMAT_ARGB8888
            )
            conn.send(pool_id, 0, buffer_payload)

            # We must wait for ``xdg_surface.configure`` before attaching
            # the buffer, otherwise labwc raises ``unconfigured_buffer``.
            # xdg_wm_base.ping must be answered with pong, otherwise the
            # compositor kills us as unresponsive.
            configured = {"done": False, "serial": 0}
            pending_serials: list[int] = []

            def on_xdg_surface_event(opcode: int, data: bytes) -> None:
                # xdg_surface.configure opcode = 0; payload is serial(u32).
                if opcode == 0:
                    (serial,) = _U32.unpack(data[:4])
                    if configured["done"]:
                        pending_serials.append(serial)  # later configure: ack it
                    configured["serial"] = serial
                    configured["done"] = True

            def on_wm_event(opcode: int, data: bytes) -> None:
                # xdg_wm_base.ping opcode = 0; payload is serial(u32).
                if opcode == 0:
                    (serial,) = _U32.unpack(data[:4])
                    # xdg_wm_base.pong(serial:uint) opcode = 3.
                    conn.send(wm_id, 3, _U32.pack(serial & 0xFFFFFFFF))

            conn.on_event(xdg_surface_id, on_xdg_surface_event)
            conn.on_event(wm_id, on_wm_event)
            # Drain globals + the initial configure (some compositors bundle
            # the configure with the post-bind roundtrip).
            # The role is only configured after an initial commit without a
            # buffer (wl_surface.commit is opcode 6).
            conn.send(surface_id, 6, b"")
            cfg_deadline = time.monotonic() + 5.0
            while not configured["done"] and time.monotonic() < cfg_deadline:
                msg = conn._drain_one()
                if msg is not None:
                    conn._dispatch(*msg)
            if not configured["done"]:
                raise WaylandError(
                    "xdg_surface.configure never arrived; compositor refuses the role"
                )
            # xdg_surface.ack_configure(serial) opcode = 4.
            conn.send(xdg_surface_id, 4, _U32.pack(configured["serial"] & 0xFFFFFFFF))
            # wl_surface.attach(buffer, x:int, y:int) opcode = 1.
            conn.send(
                surface_id,
                1,
                _U32.pack(buffer_id & 0xFFFFFFFF) + _I32.pack(0) + _I32.pack(0),
            )
            # wl_surface.commit opcode = 6.
            conn.send(surface_id, 6, b"")

            # Now create the inhibitor against the mapped surface.
            inhibitor_id = conn.alloc_id()
            # zwp_idle_inhibit_manager_v1.create_inhibitor(new_id, surface)
            # opcode = 1 (0 is the manager's destroy)
            payload = _U32.pack(inhibitor_id & 0xFFFFFFFF) + _U32.pack(
                surface_id & 0xFFFFFFFF
            )
            conn.send(inhibit_mgr_id, 1, payload)
            sys.stdout.write("inhibitor active\n")
            sys.stdout.flush()

            # Keep the connection alive for the requested duration while
            # servicing ping/pong and any further configure events.
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                # Short poll: _send() resets the socket timeout, so set it
                # again for every wait to keep the loop responsive.
                conn._sock.settimeout(0.1)
                msg = conn._drain_one()
                if msg is not None:
                    conn._dispatch(*msg)
                if pending_serials:
                    for serial in pending_serials:
                        # ack_configure opcode = 4.
                        conn.send(xdg_surface_id, 4, _U32.pack(serial & 0xFFFFFFFF))
                    pending_serials.clear()
                # Sleep the smaller of 100 ms or the time left; small enough
                # to answer pings promptly, large enough to avoid a busy loop.
                sleep_for = min(0.1, max(0.0, deadline - time.monotonic()))
                if sleep_for > 0:
                    time.sleep(sleep_for)

            # Tear the inhibitor down first (the spec says destroy it
            # before the surface it was bound to).
            conn.send(inhibitor_id, 0, b"")  # zwp_idle_inhibitor_v1.destroy
            conn.roundtrip()
            # The remaining objects die with the connection (conn.close()).
        finally:
            os.close(buf_fd)
    finally:
        conn.close()
    return 0


# ---------------------------------------------------------------------------
# CLI plumbing.
# ---------------------------------------------------------------------------


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="wl_poke",
        description="Raw-wire Wayland client for the NCZ-OS test harness.",
    )
    p.add_argument(
        "--verbose",
        action="store_true",
        help="Print a protocol trace to stderr.",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("globals", help="Dump wl_registry globals as JSON.")
    sp.set_defaults(func=cmd_globals)

    sp = sub.add_parser("motion", help="Inject relative pointer motion.")
    sp.add_argument("--dx", type=int, default=7, help="X displacement (default: 7)")
    sp.add_argument("--dy", type=int, default=3, help="Y displacement (default: 3)")
    sp.add_argument(
        "--count", type=int, default=3, help="Number of events (default: 3)"
    )
    sp.add_argument(
        "--interval",
        type=float,
        default=0.2,
        help="Seconds between events (default: 0.2)",
    )
    sp.set_defaults(func=cmd_motion)

    sp = sub.add_parser("key", help="Inject a key event via the virtual keyboard.")
    sp.add_argument(
        "--code", type=int, default=42, help="Evdev keycode (default: 42 = LShift)"
    )
    sp.add_argument(
        "--codes", default="", help="Comma separated evdev keycodes to type in order"
    )
    sp.add_argument("--count", type=int, default=1, help="Repetitions (default: 1)")
    sp.set_defaults(func=cmd_key)

    sp = sub.add_parser("click", help="Click at x,y inside a width x height extent.")
    sp.add_argument("x", type=int)
    sp.add_argument("y", type=int)
    sp.add_argument("--width", type=int, default=1536)
    sp.add_argument("--height", type=int, default=960)
    sp.add_argument("--button", type=int, default=0x110)
    sp.set_defaults(func=cmd_click)

    sp = sub.add_parser("scroll", help="Scroll vertically at x,y.")
    sp.add_argument("x", type=int)
    sp.add_argument("y", type=int)
    sp.add_argument("--amount", type=int, default=15)
    sp.add_argument("--steps", type=int, default=3)
    sp.add_argument("--width", type=int, default=1536)
    sp.add_argument("--height", type=int, default=960)
    sp.set_defaults(func=cmd_scroll)

    sp = sub.add_parser("inhibit", help="Hold an idle inhibitor for N seconds.")
    sp.add_argument(
        "--seconds",
        type=float,
        default=10.0,
        help="How long to keep the inhibitor (default: 10)",
    )
    sp.set_defaults(func=cmd_inhibit)

    return p


def main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
