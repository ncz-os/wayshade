# SPDX-License-Identifier: Apache-2.0
"""Tests for the 0.7.11 launcher fixes: offload_targets synthesis.

Recovered from feat/launcher-ux-test-fixes-2026-09-30 commit e17cf32.

Fix #2: offload_targets synthesises one nvidia target when list_gpus()
is empty but the topology says offload is possible. Without the fix,
the launcher's 'prime' mode silently degraded to display-GPU rendering.

Run: python3 -m unittest discover -s launcher/tests -v
"""

import importlib.machinery
import importlib.util
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

# `glib-compile-schemas` ships in /usr/lib/$ARCH/glib-2.0/ on Debian-based
# images but is not on the default PATH in minimal build containers. Add it
# before any setUp() runs so subprocess.run(["glib-compile-schemas", ...])
# does not raise FileNotFoundError.
_glib_compile_schemas = Path("/usr/lib/x86_64-linux-gnu/glib-2.0") / "glib-compile-schemas"
if _glib_compile_schemas.exists():
    os.environ["PATH"] = (
        f"{_glib_compile_schemas.parent}{os.pathsep}{os.environ.get('PATH', '')}"
    )

_loader = importlib.machinery.SourceFileLoader("ncz_screensaver", str(LAUNCHER))
_spec = importlib.util.spec_from_loader("ncz_screensaver", _loader)
ns = importlib.util.module_from_spec(_spec)
_loader.exec_module(ns)


class OffloadTargetsSynthesisTests(unittest.TestCase):
    """Fix #2: offload_targets synthesises one nvidia target when
    list_gpus() is empty but the topology says offload is possible."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ncz-ss-011-")
        t = Path(self.tmp)
        (t / "bin").mkdir()
        (t / "schemas").mkdir()
        shutil.copy(SCHEMA_XML, t / "schemas")
        subprocess.run(["glib-compile-schemas", str(t / "schemas")], check=True)
        os.environ["XDG_CONFIG_HOME"] = str(t / "cfg")
        os.environ["XDG_STATE_HOME"] = str(t / "st")
        os.environ["XDG_RUNTIME_DIR"] = str(t / "rt")
        os.environ["NCZ_SCREENSAVER_DIRS"] = str(t / "bin")
        os.makedirs(os.environ["XDG_RUNTIME_DIR"], mode=0o700)

    def tearDown(self):
        for k in (
            "NCZ_SCREENSAVER_SYSFS",
            "XDG_CONFIG_HOME",
            "XDG_STATE_HOME",
            "XDG_RUNTIME_DIR",
            "NCZ_SCREENSAVER_DIRS",
        ):
            os.environ.pop(k, None)
        ns.gpu_topology.cache_clear()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_prime_synthesises_nvidia_when_list_gpus_empty(self):
        """In prime mode, an empty list_gpus() but nvidia_offload True
        still produces an offload target. Pre-fix, this returned []."""
        # nvidia_offload=True topology but list_gpus() returns empty.
        empty_listing = []
        topo = {"display_class": "integrated", "nvidia_offload": True}
        with (
            mock.patch.object(ns, "list_gpus", return_value=empty_listing),
            mock.patch.object(ns, "gpu_topology", return_value=topo),
        ):
            targets = ns.offload_targets({"gpu-offload": "prime"})
        self.assertEqual(len(targets), 1)
        self.assertEqual(targets[0]["vendor"], "nvidia")

    def test_auto_does_not_synthesise_without_topology_signal(self):
        """In auto mode with no nvidia_offload signal, no synthesis."""
        empty_listing = []
        topo = {"display_class": "integrated", "nvidia_offload": False}
        with (
            mock.patch.object(ns, "list_gpus", return_value=empty_listing),
            mock.patch.object(ns, "gpu_topology", return_value=topo),
        ):
            targets = ns.offload_targets({"gpu-offload": "auto"})
        # Without nvidia_offload, auto does not synthesise; on battery,
        # auto also does not wake a second GPU.
        self.assertEqual(targets, [])

    def test_auto_synthesises_when_topology_signals_offload(self):
        """In auto mode with nvidia_offload=True, synthesise."""
        empty_listing = []
        topo = {"display_class": "integrated", "nvidia_offload": True}
        with (
            mock.patch.object(ns, "list_gpus", return_value=empty_listing),
            mock.patch.object(ns, "gpu_topology", return_value=topo),
            mock.patch.object(ns, "ac_online", return_value=True),
        ):
            targets = ns.offload_targets({"gpu-offload": "auto"})
        self.assertEqual(len(targets), 1)
        self.assertEqual(targets[0]["vendor"], "nvidia")


if __name__ == "__main__":
    unittest.main()
