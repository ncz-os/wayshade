# SPDX-License-Identifier: Apache-2.0
"""`ncz-screensaver still`: one frame of a hack through its capture mode (no GPU needed here).

A fake hack binary records the environment it was started with and writes the
PNG the capture options name, so the test checks the contract the Singularity
plugin relies on: stored options plus overrides, a fixed seed when none is
stored, a single frame at the requested size without supersampling, and clean
exit codes when the hack has no capture mode or fails.
"""

import contextlib
import importlib.machinery
import importlib.util
import io
import os
import shutil
import stat
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / "launcher" / "ncz-screensaver"

_loader = importlib.machinery.SourceFileLoader("ncz_screensaver_still", str(LAUNCHER))
_spec = importlib.util.spec_from_loader("ncz_screensaver_still", _loader)
ns = importlib.util.module_from_spec(_spec)
_loader.exec_module(ns)

SCHEMA = [
    "palette\tenum\tstylized\t\t\tstylized,cygnus\tPalette\tColors\tLook\tNCZ_BLACKHOLE_PALETTE",
    "exposure\tfloat\t1\t0.2\t4\t\tExposure\tBrightness\tLook\tNCZ_BLACKHOLE_EXPOSURE",
    "seed\tint\t0\t0\t999999\t\tSeed\tSeed\tSystem\tNCZ_BLACKHOLE_SEED",
    "capture\tstring\t\t\t\t\tCapture\tPath\tCapture\tNCZ_BLACKHOLE_CAPTURE",
    "capture-at\tfloat\t0\t0\t36000\t\tAt\tTime\tCapture\tNCZ_BLACKHOLE_CAPTURE_AT",
    "capture-frames\tint\t1\t1\t100000\t\tFrames\tN\tCapture\tNCZ_BLACKHOLE_CAPTURE_FRAMES",
    "capture-fps\tfloat\t30\t0.001\t240\t\tFps\tRate\tCapture\tNCZ_BLACKHOLE_CAPTURE_FPS",
    "capture-size\tstring\t3840x2160\t\t\t\tSize\tWxH\tCapture\tNCZ_BLACKHOLE_CAPTURE_SIZE",
    "capture-supersample\tint\t3\t1\t8\t\tSS\tN\tCapture\tNCZ_BLACKHOLE_CAPTURE_SUPERSAMPLE",
    "capture-steps\tint\t700\t80\t4000\t\tSteps\tN\tCapture\tNCZ_BLACKHOLE_CAPTURE_STEPS",
]

FAKE_HACK = """#!/bin/sh
# fake blackhole_gles3: dump the environment and write the capture file
env | grep '^NCZ_' | sort > "$FAKE_ENV_OUT"
[ -n "$NCZ_BLACKHOLE_CAPTURE" ] || exit 7
[ "${FAKE_FAIL:-0}" = 1 ] && exit 9
printf 'PNG' > "$NCZ_BLACKHOLE_CAPTURE"
exit 0
"""

NO_PLAN = {
    "gpu": None,
    "offload": False,
    "class": "weak",
    "copy_ms": 0.0,
    "source": "test",
}


class StillTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="ncz-still-"))
        (self.tmp / "options").mkdir()
        (self.tmp / "options" / "blackhole.tsv").write_text("\n".join(SCHEMA) + "\n")
        (self.tmp / "options" / "voronoi.tsv").write_text(
            "speed\tfloat\t1\t0.1\t4\t\tSpeed\tTime scale\tLook\tNCZ_VORONOI_SPEED\n"
        )
        self.bins = self.tmp / "bin"
        self.bins.mkdir()
        for name in ("blackhole_gles3", "voronoi_gles3"):
            p = self.bins / name
            p.write_text(FAKE_HACK)
            p.chmod(p.stat().st_mode | stat.S_IXUSR)
        self.env_out = self.tmp / "env.txt"
        self.env = {
            "NCZ_SCREENSAVER_OPTIONS": str(self.tmp / "options"),
            "NCZ_SCREENSAVER_DIRS": str(self.bins),
            "FAKE_ENV_OUT": str(self.env_out),
            "XDG_RUNTIME_DIR": str(self.tmp / "rt"),
        }
        self.patches = [
            mock.patch.dict(os.environ, self.env),
            mock.patch.object(ns, "display_plan", return_value=NO_PLAN),
            mock.patch.object(ns, "plan_for", return_value=NO_PLAN),
            mock.patch.object(ns, "default_render_cap", return_value=0),
        ]
        for p in self.patches:
            p.start()

    def tearDown(self):
        for p in reversed(self.patches):
            p.stop()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def child_env(self):
        return dict(
            line.split("=", 1) for line in self.env_out.read_text().splitlines()
        )

    def test_supported_only_with_the_capture_options(self):
        bh = {r["name"]: r for r in ns.load_option_schema("blackhole_gles3")}
        vo = {r["name"]: r for r in ns.load_option_schema("voronoi_gles3")}
        self.assertTrue(ns.still_supported(bh))
        self.assertFalse(ns.still_supported(vo))

    def test_renders_with_stored_options_overrides_and_fixed_seed(self):
        settings = dict(ns.DEFAULTS)
        settings["hack-options"] = {"blackhole_gles3": {"palette": "cygnus"}}
        out = self.tmp / "still.png"
        code, msg = ns.render_still(
            "blackhole_gles3",
            settings,
            "480x270",
            out,
            at=12.5,
            steps=120,
            overrides={"exposure": "1.5"},
        )
        self.assertEqual((code, msg), (0, str(out)))
        self.assertEqual(out.read_text(), "PNG")
        env = self.child_env()
        self.assertEqual(env["NCZ_BLACKHOLE_PALETTE"], "cygnus")
        self.assertEqual(env["NCZ_BLACKHOLE_EXPOSURE"], "1.5")
        self.assertEqual(env["NCZ_BLACKHOLE_SEED"], str(ns.STILL_SEED))
        self.assertEqual(env["NCZ_BLACKHOLE_CAPTURE_SIZE"], "480x270")
        self.assertEqual(env["NCZ_BLACKHOLE_CAPTURE_FRAMES"], "1")
        self.assertEqual(env["NCZ_BLACKHOLE_CAPTURE_SUPERSAMPLE"], "1")
        self.assertEqual(env["NCZ_BLACKHOLE_CAPTURE_STEPS"], "120")
        self.assertEqual(env["NCZ_BLACKHOLE_CAPTURE_AT"], "12.5")
        self.assertTrue(env["NCZ_BLACKHOLE_CAPTURE"].endswith(".part.png"))
        self.assertEqual(env["NCZ_HARNESS_HEADLESS"], "480x270")
        self.assertNotIn("NCZ_SCREENSAVER_ACTIVE", env)
        # no leftover temporary file
        self.assertEqual(sorted(p.name for p in self.tmp.glob("still*")), ["still.png"])

    def test_stored_seed_wins_over_the_fixed_one(self):
        settings = dict(ns.DEFAULTS)
        settings["hack-options"] = {"blackhole_gles3": {"seed": "42"}}
        code, _ = ns.render_still(
            "blackhole_gles3", settings, "64x64", self.tmp / "s.png"
        )
        self.assertEqual(code, 0)
        self.assertEqual(self.child_env()["NCZ_BLACKHOLE_SEED"], "42")

    def test_unsupported_hack_bad_size_and_failure_codes(self):
        settings = dict(ns.DEFAULTS)
        self.assertEqual(
            ns.render_still("voronoi_gles3", settings, "64x64", self.tmp / "v.png")[0],
            4,
        )
        self.assertEqual(
            ns.render_still("blackhole_gles3", settings, "huge", self.tmp / "h.png")[0],
            2,
        )
        self.assertEqual(
            ns.render_still("blackhole_gles3", settings, "8x8", self.tmp / "h.png")[0],
            2,
        )
        with mock.patch.dict(os.environ, {"FAKE_FAIL": "1"}):
            code, msg = ns.render_still(
                "blackhole_gles3", settings, "64x64", self.tmp / "f.png"
            )
        self.assertEqual(code, 5)
        self.assertIn("exit 9", msg)
        self.assertFalse((self.tmp / "f.png").exists())

    def test_cli_parser_has_still(self):
        args = ns.build_parser().parse_args(
            ["still", "blackhole_gles3", "--size", "320x180", "--at", "5"]
        )
        self.assertEqual(args.fn, ns.cmd_still)
        self.assertEqual((args.size, args.at, args.steps), ("320x180", 5.0, 160))
        self.assertEqual(ns.build_parser().parse_args(["still", "x_gles3"]).at, 12.0)

    def test_cmd_still_default_output_in_runtime_dir(self):
        catalog = [{"id": "blackhole_gles3", "title": "Black Hole", "group": "g"}]
        with (
            mock.patch.object(
                ns, "load_settings", return_value=(dict(ns.DEFAULTS), True)
            ),
            mock.patch.object(ns, "load_catalog", return_value=catalog),
        ):
            args = ns.build_parser().parse_args(
                ["still", "blackhole_gles3", "--size", "64x64"]
            )
            printed = io.StringIO()
            with contextlib.redirect_stdout(printed):
                self.assertEqual(args.fn(args), 0)
            expect = (
                Path(self.env["XDG_RUNTIME_DIR"])
                / "ncz-screensaver"
                / "still-blackhole_gles3.png"
            )
            self.assertEqual(printed.getvalue().strip(), str(expect))
            self.assertEqual(expect.read_text(), "PNG")
            args = ns.build_parser().parse_args(["still", "nope_gles3"])
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(args.fn(args), 1)


if __name__ == "__main__":
    unittest.main()
