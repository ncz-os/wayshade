# SPDX-License-Identifier: Apache-2.0
"""Tests for the 0.7.11 launcher fixes: dumped_schema timeout + Supervisor pre-launch publish.

Recovered from feat/launcher-ux-test-fixes-2026-09-30 commit e17cf32.

Two related fixes:

  * `Supervisor.run` writes the picked hack into state.json *before*
    launching the child, so a parent that observes the supervisor pid
    also sees which hack is about to run (e.g. on slow first-run
    `<hack> --dump-schema` probes).

  * `dumped_schema` timeout drops from 5 s to 1 s. A binary that
    ignores the flag must not stall the supervisor for a full
    cycle-delay.

Run: python3 -m unittest discover -s launcher/tests -v
"""

import importlib.machinery
import importlib.util
import inspect
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / "launcher" / "ncz-screensaver"
SCHEMA_XML = ROOT / "config" / "dev.ncz.screensaver.gschema.xml"

_glib_compile_schemas = Path("/usr/lib/x86_64-linux-gnu/glib-2.0") / "glib-compile-schemas"
if _glib_compile_schemas.exists():
    os.environ["PATH"] = (
        f"{_glib_compile_schemas.parent}{os.pathsep}{os.environ.get('PATH', '')}"
    )

_loader = importlib.machinery.SourceFileLoader("ncz_screensaver", str(LAUNCHER))
_spec = importlib.util.spec_from_loader("ncz_screensaver", _loader)
ns = importlib.util.module_from_spec(_spec)
_loader.exec_module(ns)


class DumpedSchemaTimeoutTests(unittest.TestCase):
    """Fix #5: dumped_schema timeout is 1 s (not 5 s)."""

    def test_subprocess_timeout_is_one_second(self):
        """Verify the source of dumped_schema uses timeout=1."""
        src = inspect.getsource(ns.dumped_schema)
        self.assertIn("timeout=1", src)
        # The pre-fix code had timeout=5; reject that explicitly.
        self.assertNotIn("timeout=5", src)


class SupervisorPublishesHackBeforeLaunchTests(unittest.TestCase):
    """Fix #4: Supervisor.run writes the picked hack into state.json
    *before* launching the child. Without the fix, state.json had
    hack=None for the duration of pick() + launch()."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ncz-ss-011-")
        t = Path(self.tmp)
        (t / "bin").mkdir()
        (t / "schemas").mkdir()
        shutil.copy(SCHEMA_XML, t / "schemas")
        subprocess.run(["glib-compile-schemas", str(t / "schemas")], check=True)
        for name, body in (
            ("ok1", "exec sleep 60"),
            ("ok2", "exec sleep 60"),
            ("crash", "exit 1"),
        ):
            f = t / "bin" / name
            f.write_text(f"#!/bin/sh\n{body}\n")
            f.chmod(0o755)
        (t / "cat.tsv").write_text("ok1\tOK1\tg\tg\nok2\tOK2\tg\tg\ncrash\tC\tg\tg\n")
        self.env = dict(
            os.environ,
            GSETTINGS_BACKEND="keyfile",
            GSETTINGS_SCHEMA_DIR=str(t / "schemas"),
            XDG_CONFIG_HOME=str(t / "cfg"),
            XDG_RUNTIME_DIR=str(t / "rt"),
            XDG_STATE_HOME=str(t / "st"),
            NCZ_SCREENSAVER_DIRS=str(t / "bin"),
            NCZ_SCREENSAVER_CATALOG=str(t / "cat.tsv"),
            NCZ_SCREENSAVER_MIN_CYCLE="1",
            NCZ_SCREENSAVER_PROC_ROOT=str(t / "noproc"),
            NCZ_SCREENSAVER_VERIFY_DELAY="3600",  # do not verify mid-run
        )
        os.makedirs(self.env["XDG_RUNTIME_DIR"], mode=0o700)

    def tearDown(self):
        # Stop any lingering supervisor.
        subprocess.run(
            [str(LAUNCHER), "stop"],
            env=self.env,
            capture_output=True,
            timeout=10,
            check=False,
        )
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_picked_hack_is_written_before_run_one_sets_child_pid(self):
        """The fix writes `self.child_hack = hack; self.write_state(None)`
        between `pick()` and `run_one()`. With the fix, the pre-run_one
        write_state has hack=ok1 but child_pid=None."""
        # Build a Supervisor with a stub args/settings.
        from argparse import Namespace
        args = Namespace(hack="ok1", seconds=None, option=None)
        settings = {
            "mode": "one",
            "hack-id": "ok1",
            "cycle-delay": 60,
            "verify-render": False,
        }
        catalog = []
        rt = Path(self.env["XDG_RUNTIME_DIR"]) / "ncz-screensaver"
        rt.mkdir(parents=True, exist_ok=True)

        # Patch write_state so the *first* write_state inside run() (with
        # hack set) is captured before run_one has any chance to overwrite
        # it. The second write_state (in run_one) is replaced by a no-op so
        # we keep observing the first.
        captured = {}
        original_write_state = ns.Supervisor.write_state

        def capture_state(self, rotates_at=None):
            # Skip the write inside run_one (which overwrites with child_pid).
            if self.child is not None:
                return
            path = self.rt / "state.json"
            original_write_state(self, rotates_at)
            try:
                captured["state"] = json.loads(path.read_text())
            except (FileNotFoundError, json.JSONDecodeError):
                captured["state"] = None

        # Patch run_one to be a no-op so the supervisor stays in the loop.
        def fake_run_one(self, hack_id, deadline):
            self.stop_requested = True
            return "stop"

        sup = ns.Supervisor(args, settings, catalog)

        with (
            mock.patch.object(ns.Supervisor, "write_state", capture_state),
            mock.patch.object(ns.Supervisor, "run_one", fake_run_one),
            mock.patch.object(ns.Supervisor, "calibrate", lambda self: None),
            mock.patch.object(ns.Supervisor, "stop_child", lambda self: None),
        ):
            sup.run()

        self.assertIsNotNone(captured.get("state"), "write_state was never called")
        self.assertEqual(
            captured["state"].get("hack"),
            "ok1",
            "Supervisor.run did not write the picked hack to state.json before "
            "calling run_one; the fix is not in place",
        )


if __name__ == "__main__":
    unittest.main()
