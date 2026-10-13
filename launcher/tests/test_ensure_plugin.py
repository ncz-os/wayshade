# SPDX-License-Identifier: Apache-2.0
"""ncz-screensaver ensure-plugin: enables the Singularity Settings plugin once per
user at login, and says why whenever it does nothing.

Run: python3 -m unittest discover -s launcher/tests -v
"""

import contextlib
import importlib.machinery
import importlib.util
import io
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / "launcher" / "ncz-screensaver"
_loader = importlib.machinery.SourceFileLoader("ncz_screensaver_ep", str(LAUNCHER))
_spec = importlib.util.spec_from_loader("ncz_screensaver_ep", _loader)
ns = importlib.util.module_from_spec(_spec)
_loader.exec_module(ns)


def cp(rc=0, out="", err=""):
    return subprocess.CompletedProcess(["gsettings"], rc, out, err)


class EnsurePluginTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.so = Path(self.tmp.name) / "libscreensaver.so"
        self.so.write_text("x")
        self.state = Path(self.tmp.name) / "state"
        self.marker = self.state / "ncz-screensavers" / "plugin-enabled"
        patches = [
            mock.patch.object(ns, "PLUGIN_SO", str(self.so)),
            mock.patch.dict(os.environ, {"XDG_STATE_HOME": str(self.state)}),
            mock.patch.object(ns.shutil, "which", return_value="/usr/bin/gsettings"),
        ]
        for p in patches:
            p.start()
            self.addCleanup(p.stop)

    def run_cmd(self, gs):
        err = io.StringIO()
        with (
            mock.patch.object(ns, "shell_gsettings", side_effect=gs) as m,
            contextlib.redirect_stderr(err),
        ):
            rc = ns.cmd_ensure_plugin(None)
        return rc, err.getvalue(), m

    def test_missing_plugin_is_logged_not_silent(self):
        self.so.unlink()
        rc, err, m = self.run_cmd([])
        self.assertEqual(rc, 0)
        self.assertIn("not installed", err)
        self.assertIn("ncz-screensavers-plugin", err)
        m.assert_not_called()
        self.assertFalse(self.marker.exists())

    def test_missing_gsettings_is_logged(self):
        with mock.patch.object(ns.shutil, "which", return_value=None):
            rc, err, _ = self.run_cmd([])
        self.assertEqual(rc, 0)
        self.assertIn("gsettings not found", err)

    def test_enables_once_and_records_marker(self):
        rc, err, m = self.run_cmd([cp(out="['sensors']\n"), cp()])
        self.assertEqual(rc, 0)
        self.assertEqual(
            m.call_args_list[1].args,
            ("set", ns.SHELL_SCHEMA, "enabled-plugins", "['sensors', 'screensaver']"),
        )
        self.assertIn("enabled the Settings plugin", err)
        self.assertTrue(self.marker.exists())
        # second login: marker present, nothing touched, nothing logged
        rc, err, m = self.run_cmd([])
        self.assertEqual((rc, err), (0, ""))
        m.assert_not_called()

    def test_already_enabled_records_marker_without_set(self):
        rc, _, m = self.run_cmd([cp(out="['screensaver', 'sensors']\n")])
        self.assertEqual(rc, 0)
        self.assertEqual(m.call_count, 1)
        self.assertTrue(self.marker.exists())

    def test_schema_unavailable_is_logged(self):
        rc, err, _ = self.run_cmd([cp(rc=1, err="No such schema")])
        self.assertEqual(rc, 0)
        self.assertIn("not available", err)
        self.assertIn("No such schema", err)
        self.assertFalse(self.marker.exists())

    def test_unparsable_list_is_logged(self):
        rc, err, _ = self.run_cmd([cp(out="[[[\n")])
        self.assertEqual(rc, 0)
        self.assertIn("cannot parse", err)
        self.assertFalse(self.marker.exists())

    def test_non_list_value_is_logged(self):
        rc, err, _ = self.run_cmd([cp(out="true\n")])
        self.assertEqual(rc, 0)
        self.assertIn("not a list", err)

    def test_set_failure_returns_1_and_retries_next_login(self):
        rc, err, _ = self.run_cmd([cp(out="['sensors']\n"), cp(rc=1, err="denied")])
        self.assertEqual(rc, 1)
        self.assertIn("could not enable", err)
        self.assertFalse(self.marker.exists())


if __name__ == "__main__":
    unittest.main()
