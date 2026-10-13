# SPDX-License-Identifier: Apache-2.0
"""Tests for the 0.7.11 launcher fixes: render_env source gate + system_class cache priority.

Recovered from feat/launcher-ux-test-fixes-2026-09-30 commit e17cf32.

Two related fixes:

  * render_env does not apply WEAK_DEFAULT_SCALE when the class came
    from a topology-table guess (`source == "table"`). Without the
    fix, the launcher's "weak" path suppressed NCZ_RENDER_SCALE on
    hosts that had never been benchmarked.

  * system_class trusts a cached measurement over the gpu_topology
    `display_class` fallback. Seeded test fixtures (and real
    benchmarks) win over the topology table guess.

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

_glib_compile_schemas = Path("/usr/lib/x86_64-linux-gnu/glib-2.0") / "glib-compile-schemas"
if _glib_compile_schemas.exists():
    os.environ["PATH"] = (
        f"{_glib_compile_schemas.parent}{os.pathsep}{os.environ.get('PATH', '')}"
    )

_loader = importlib.machinery.SourceFileLoader("ncz_screensaver", str(LAUNCHER))
_spec = importlib.util.spec_from_loader("ncz_screensaver", _loader)
ns = importlib.util.module_from_spec(_spec)
_loader.exec_module(ns)


def _setup_tmp():
    """Common setUp helper: build a temp dir with a compiled schema."""
    tmp = tempfile.mkdtemp(prefix="ncz-ss-011-")
    t = Path(tmp)
    (t / "bin").mkdir()
    (t / "schemas").mkdir()
    shutil.copy(SCHEMA_XML, t / "schemas")
    subprocess.run(["glib-compile-schemas", str(t / "schemas")], check=True)
    os.environ["XDG_CONFIG_HOME"] = str(t / "cfg")
    os.environ["XDG_STATE_HOME"] = str(t / "st")
    os.environ["XDG_RUNTIME_DIR"] = str(t / "rt")
    os.environ["NCZ_SCREENSAVER_DIRS"] = str(t / "bin")
    os.makedirs(os.environ["XDG_RUNTIME_DIR"], mode=0o700)
    return tmp


class RenderEnvSourceFieldTests(unittest.TestCase):
    """Fix #3: render_env does not force WEAK_DEFAULT_SCALE when the
    class came from a topology-table guess (source == 'table')."""

    def setUp(self):
        self.tmp = _setup_tmp()

    def tearDown(self):
        for k in (
            "XDG_CONFIG_HOME",
            "XDG_STATE_HOME",
            "XDG_RUNTIME_DIR",
            "NCZ_SCREENSAVER_DIRS",
            "NCZ_SCREENSAVER_SYSFS",
        ):
            os.environ.pop(k, None)
        ns.gpu_topology.cache_clear()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_source_table_does_not_apply_weak_default_scale(self):
        """render_env with plan['source'] == 'table' must not inject
        NCZ_RENDER_SCALE=0.5 even when class is 'weak'."""
        plan = {"class": "weak", "source": "table", "copy_ms": 0.0, "offload": False, "gpu": None}
        env = ns.render_env(
            "xshadertoy_x_gles3",
            {"render-scale-mode": "auto"},
            plan=plan,
        )
        # Pre-fix, NCZ_RENDER_SCALE was forced to 0.5 whenever class=='weak';
        # post-fix, source=='table' (a topology-table guess, not a real
        # measurement) means we leave the scale alone.
        self.assertNotIn("NCZ_RENDER_SCALE", env)

    def test_source_calibration_applies_weak_default_scale(self):
        """render_env with plan['source'] == 'calibration' still applies
        the weak default scale when class is 'weak' (real measurement)."""
        plan = {"class": "weak", "source": "calibration", "copy_ms": 0.0, "offload": False, "gpu": None}
        env = ns.render_env(
            "xshadertoy_x_gles3",
            {"render-scale-mode": "auto"},
            plan=plan,
        )
        # A real measurement of 'weak' keeps the pre-fix behaviour:
        # NCZ_RENDER_SCALE=0.5 is still applied (format may be 0.5 or 0.50).
        self.assertIn(env.get("NCZ_RENDER_SCALE"), ("0.5", "0.50"))


class SystemClassCachePriorityTests(unittest.TestCase):
    """Fix #6: system_class trusts a cached measurement over the
    gpu_topology 'display_class' fallback."""

    def setUp(self):
        self.tmp = _setup_tmp()

    def tearDown(self):
        for k in (
            "XDG_CONFIG_HOME",
            "XDG_STATE_HOME",
            "XDG_RUNTIME_DIR",
            "NCZ_SCREENSAVER_DIRS",
            "NCZ_SCREENSAVER_SYSFS",
        ):
            os.environ.pop(k, None)
        ns.gpu_topology.cache_clear()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_cache_strong_overrides_topology_weak(self):
        """When the cache holds 'strong' but topology guesses 'weak',
        system_class returns 'strong' (the cache wins)."""
        # Build a fake cache that says the display GPU is 'strong' but
        # gpu_topology reports 'weak' (display_class='integrated').
        with (
            mock.patch.object(
                ns, "gpu_topology",
                return_value={"display_class": "integrated"},
            ),
            mock.patch.object(ns, "read_class_cache", return_value={"/dev/dri/card0": {"class": "strong"}}),
            mock.patch.object(ns, "_cache_key", return_value="/dev/dri/card0"),
        ):
            cls = ns.system_class({})
        self.assertEqual(cls, "strong")

    def test_no_cache_falls_back_to_topology(self):
        """When the cache is empty, system_class defers to gpu_topology."""
        with (
            mock.patch.object(
                ns, "gpu_topology",
                return_value={"display_class": "integrated"},
            ),
            mock.patch.object(ns, "read_class_cache", return_value={}),
            mock.patch.object(ns, "_cache_key", return_value="/dev/dri/card0"),
        ):
            cls = ns.system_class({})
        # Topology 'integrated' -> 'weak'.
        self.assertEqual(cls, "weak")


if __name__ == "__main__":
    unittest.main()
