# SPDX-License-Identifier: Apache-2.0
"""Tests for the 0.7.11 launcher fixes (recovered from
feat/launcher-ux-test-fixes-2026-09-30 commit e17cf32).

These exercise the per-fix changes that were applied to feat/launcher-ux
by the recovery branch `feat/launcher-ux-recover-fixes-2026-10-01`:

  1. `plan_for` normalises legacy tier names ('igpu'/'discrete') the
     same way `filter_pool` does. Without the fix, a `tiers` argument
     containing a legacy name crashed `CLASS_RANK[...]` with KeyError.

  2. `offload_targets` synthesises one nvidia target when list_gpus()
     is empty but gpu_topology reports nvidia_offload (or the user has
     forced prime / a GPU id). Without the fix, the launcher's 'prime'
     mode silently degraded to display-GPU rendering.

  3. `render_env` does not apply WEAK_DEFAULT_SCALE when the class
     came from a topology-table guess (`source == "table"`). Without
     the fix, the launcher's "weak" path suppressed NCZ_RENDER_SCALE
     on hosts that had never been benchmarked.

  4. `Supervisor.run` writes the picked hack into state.json *before*
     launching the child, so a parent that observes the supervisor pid
     also sees which hack is about to run (e.g. on slow first-run
     `<hack> --dump-schema` probes).

  5. `dumped_schema` timeout drops from 5 s to 1 s. A binary that
     ignores the flag must not stall the supervisor for a full
     cycle-delay.

  6. `system_class` trusts a cached measurement over the gpu_topology
     `display_class` fallback. Seeded test fixtures (and real
     benchmarks) win over the topology table guess.

Run: python3 -m unittest discover -s launcher/tests -v
(Minimal build images that lack /usr/lib/$ARCH/glib-2.0 on $PATH are
handled at module load: see the PATH augmentation below.)
"""

import importlib.machinery
import importlib.util
import inspect
import json
import os
import shutil
import signal
import subprocess
import tempfile
import time
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

    def _display(self):
        return ns.display_gpu()

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
            except KeyError as e:  # pragma: no cover - this is what we are testing
                self.fail(f"plan_for crashed on legacy 'igpu' tier: {e}")
        # In prime mode we land on the offload target.
        self.assertTrue(plan["offload"])

    def test_legacy_discrete_tier_does_not_crash(self):
        """A tier of 'discrete' (legacy strong) must not raise KeyError."""
        with mock.patch.object(ns, "ac_online", return_value=True):
            try:
                plan = ns.plan_for(
                    "xshadertoy_x_gles3",
                    {"gpu-offload": "prime"},
                    tiers={"xshadertoy_x_gles3": "discrete"},
                )
            except KeyError as e:  # pragma: no cover - this is what we are testing
                self.fail(f"plan_for crashed on legacy 'discrete' tier: {e}")
        self.assertTrue(plan["offload"])

    def test_legacy_tiers_map_to_current_names(self):
        """Legacy tier values are normalised before CLASS_RANK lookup."""
        with mock.patch.object(ns, "ac_online", return_value=True):
            # 'igpu' -> weak ; 'discrete' -> strong. The legacy entry must
            # not appear in the CLASS_RANK lookup.
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
        # Both succeeded (no KeyError). The class of the offload target
        # is the GPU's class, not the tier name (since prime forces the
        # offload target). The point of the test is that no exception
        # leaked and the plan was returned.
        self.assertIn("class", plan_igpu)
        self.assertIn("class", plan_discrete)


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


class RenderEnvSourceFieldTests(unittest.TestCase):
    """Fix #3: render_env does not force WEAK_DEFAULT_SCALE when the
    class came from a topology-table guess (source == 'table')."""

    def test_no_render_scale_when_source_is_table(self):
        """A 'table' source means we have not measured the GPU; do not
        apply the weak default scale."""
        table_plan = {
            "gpu": None,
            "offload": False,
            "class": "weak",
            "copy_ms": 0.0,
            "source": "table",
        }
        with (
            mock.patch.object(ns, "default_render_cap", return_value=1080),
            mock.patch.object(ns, "plan_for", return_value=table_plan),
        ):
            env = ns.render_env(
                "blackhole_gles3", {"render-scale-mode": "auto"}
            )
        # The weak default scale would set NCZ_RENDER_SCALE=0.5. With
        # the fix, the 'table' source suppresses that.
        self.assertNotIn("NCZ_RENDER_SCALE", env)

    def test_render_scale_applied_when_source_is_calibration(self):
        """A 'calibration' source (real measurement) still gets the
        weak default scale."""
        cal_plan = {
            "gpu": None,
            "offload": False,
            "class": "weak",
            "copy_ms": 0.0,
            "source": "calibration",
        }
        with (
            mock.patch.object(ns, "default_render_cap", return_value=1080),
            mock.patch.object(ns, "plan_for", return_value=cal_plan),
        ):
            env = ns.render_env(
                "blackhole_gles3", {"render-scale-mode": "auto"}
            )
        self.assertEqual(env.get("NCZ_RENDER_SCALE"), "0.50")


class SystemClassCachePriorityTests(unittest.TestCase):
    """Fix #6: system_class trusts a cached measurement over the
    gpu_topology display_class fallback."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ncz-ss-011-")
        t = Path(self.tmp)
        (t / "bin").mkdir()
        (t / "schemas").mkdir()
        shutil.copy(SCHEMA_XML, t / "schemas")
        subprocess.run(["glib-compile-schemas", str(t / "schemas")], check=True)
        # Display is an integrated card.
        make_sysfs(t, [("i915", True, None)])
        os.environ["NCZ_SCREENSAVER_SYSFS"] = str(t)
        os.environ["XDG_CONFIG_HOME"] = str(t / "cfg")
        os.environ["XDG_STATE_HOME"] = str(t / "st")
        os.environ["XDG_RUNTIME_DIR"] = str(t / "rt")
        os.environ["NCZ_SCREENSAVER_DIRS"] = str(t / "bin")
        os.makedirs(os.environ["XDG_RUNTIME_DIR"], mode=0o700)
        ns.gpu_topology.cache_clear()

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

    def test_cached_strong_overrides_topology_weak(self):
        """A cache entry of 'strong' for the display GPU wins over the
        topology 'integrated' fallback."""
        display = ns.display_gpu()
        key = ns._cache_key(display)
        # Seed the cache with a 'strong' measurement for the display GPU.
        cache = {
            "entries": {
                key: {
                    "class": "strong",
                    "ms": 3.5,
                    "renderer": "Test GPU",
                    "version": "OpenGL ES 3.2 test",
                    "timer": "gpu",
                    "source": "calibration",
                    "when": "2026-10-01T00:00:00-0400",
                    "gpu": key,
                    "driver": "test",
                }
            }
        }
        cache_file = Path(self.tmp) / "class.json"
        cache_file.write_text(json.dumps(cache))
        # Point read_class_cache() at our seeded file via XDG_STATE_HOME.
        with mock.patch.object(
            ns, "read_class_cache", return_value=cache["entries"]
        ):
            cls = ns.system_class({})
        # Cache says 'strong'; the pre-fix code would consult the
        # topology table's display_class=='integrated' fallback instead.
        self.assertEqual(cls, "strong")

    def test_no_cache_falls_back_to_topology(self):
        """Without a cache entry, system_class falls back to gpu_topology's
        display_class field."""
        # Topology reports discrete for the display.
        topo = {"display_class": "discrete", "nvidia_offload": False}
        with (
            mock.patch.object(ns, "read_class_cache", return_value={}),
            mock.patch.object(ns, "gpu_topology", return_value=topo),
            mock.patch.object(ns, "gpu_class", return_value={"class": "mid"}),
        ):
            cls = ns.system_class({})
        self.assertEqual(cls, "strong")


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
        write_state has hack=ok1 but child_pid=None. The fix is verified
        by mocking `Supervisor.run_one` to capture state.json immediately
        after the outer write_state but before run_one has had a chance
        to overwrite it."""

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

        # After run() completes (with run_one no-op), the captured state
        # should have hack set. The fix path writes the hack *before*
        # run_one is called, so we expect hack=ok1 in the captured state.
        # The pre-fix path had hack=None until run_one set child_hack and
        # wrote state again; with the no-op run_one the captured state
        # would have hack=None (the initial write_state(None)).
        self.assertIsNotNone(captured.get("state"), "write_state was never called")
        self.assertEqual(
            captured["state"].get("hack"),
            "ok1",
            "Supervisor.run did not write the picked hack to state.json before "
            "calling run_one; the fix is not in place",
        )


if __name__ == "__main__":
    unittest.main()
