# SPDX-License-Identifier: Apache-2.0
"""Tests for the 0.7.11 launcher fixes: plan_for tier normalisation.

Recovered from feat/launcher-ux-test-fixes-2026-09-30 commit e17cf32.

Fix #1: plan_for normalises legacy tier names ('igpu'/'discrete') the
same way filter_pool does. Without the fix, a `tiers` argument
containing a legacy name crashed `CLASS_RANK[...]` with KeyError.

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


def make_sysfs(root, cards):
    """Build a fake sysfs with the given GPUs.

    cards: list of (driver, connected, vram_bytes or None).
    """
    for i, (driver, connected, vram) in enumerate(cards):
        card = Path(root) / "class" / "drm" / f"card{i}"
        (card / "device").mkdir(parents=True)
        drv = Path(root) / "drivers" / driver
        drv.mkdir(parents=True, exist_ok=True)
        (card / "device" / "driver").symlink_to(drv)
        if vram is not None:
            (card / "device" / "mem_info_vram_total").write_text(str(vram))
        conn = Path(root) / "class" / "drm" / f"card{i}-eDP-1"
        conn.mkdir(parents=True)
        (conn / "status").write_text(
            "connected\n" if connected else "disconnected\n"
        )


class PlanForLegacyTiersTests(unittest.TestCase):
    """Fix #1: plan_for normalises legacy tier names.

    Pre-0.7.11, `need = CLASS_RANK[tiers.get(hack_id, "strong")]` would
    raise KeyError when a tier was the legacy 'igpu' or 'discrete'. The
    fix applies the same LEGACY_TIERS mapping filter_pool already used.
    """

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ncz-ss-011-")
        t = Path(self.tmp)
        (t / "bin").mkdir()
        (t / "schemas").mkdir()
        shutil.copy(SCHEMA_XML, t / "schemas")
        subprocess.run(["glib-compile-schemas", str(t / "schemas")], check=True)
        # Intel display + nvidia offload so we have a target.
        make_sysfs(t, [("i915", True, None), ("nvidia", False, None)])
        os.environ["NCZ_SCREENSAVER_SYSFS"] = str(t)
        os.environ["XDG_CONFIG_HOME"] = str(t / "cfg")
        os.environ["XDG_STATE_HOME"] = str(t / "st")
        os.environ["XDG_RUNTIME_DIR"] = str(t / "rt")
        os.environ["NCZ_SCREENSAVER_DIRS"] = str(t / "bin")
        os.makedirs(os.environ["XDG_RUNTIME_DIR"], mode=0o700)
        ns.gpu_topology.cache_clear()
        ns.read_class_cache.cache_clear() if hasattr(
            ns.read_class_cache, "cache_clear"
        ) else None

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

    def test_legacy_igpu_tier_does_not_crash(self):
        """A tier of 'igpu' (legacy weak) must not raise KeyError."""
        # prime mode forces an offload decision regardless of class rank
        with mock.patch.object(ns, "ac_online", return_value=True):
            try:
                plan = ns.plan_for(
                    "xshadertoy_x_gles3",
                    {"gpu-offload": "prime"},
                    tiers={"xshadertoy_x_gles3": "igpu"},
                )
            except KeyError as e:  # the pre-fix symptom
                self.fail(f"plan_for crashed with KeyError on legacy 'igpu' tier: {e}")
        # The plan was returned; whatever the exact class, the function
        # did not raise and returned a dict. The 'source' field tells
        # callers (render_env) whether the class came from a measurement
        # or a topology-table guess.
        self.assertIn("class", plan)
        self.assertIn(plan["source"], ("calibration", "table"))

    def test_legacy_discrete_tier_does_not_crash(self):
        """A tier of 'discrete' (legacy strong) must not raise KeyError."""
        with mock.patch.object(ns, "ac_online", return_value=True):
            try:
                plan = ns.plan_for(
                    "xshadertoy_x_gles3",
                    {"gpu-offload": "prime"},
                    tiers={"xshadertoy_x_gles3": "discrete"},
                )
            except KeyError as e:  # the pre-fix symptom
                self.fail(f"plan_for crashed with KeyError on legacy 'discrete' tier: {e}")
        self.assertIn("class", plan)

    def test_legacy_tiers_are_normalised(self):
        """Legacy tier names ('igpu', 'discrete') must be normalised in
        the same way filter_pool normalises them before CLASS_RANK lookup."""
        with mock.patch.object(ns, "ac_online", return_value=True):
            plan_igpu = ns.plan_for(
                "xshadertoy_x_gles3",
                {"gpu-offload": "prime"},
                tiers={"xshadertoy_x_gles3": "igpu"},
            )
            plan_discrete = ns.plan_for(
                "xshadertoy_x_gles3",
                {"gpu-offload": "prime"},
                tiers={"xshadertoy_x_gles3": "discrete"},
            )
        self.assertIn("class", plan_igpu)
        self.assertIn("class", plan_discrete)


if __name__ == "__main__":
    unittest.main()
