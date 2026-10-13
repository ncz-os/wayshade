#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Regression guard: no hack may issue a periodic glReadPixels in its render
path. A readback is a synchronous GPU drain; every 30th/60th frame it froze
the animation for 55-110 ms on Orion O6N (Mali-G720) and ~45 ms on radeonsi. Frame
4 / one-shot samples are fine; periodic ones must go through
ncz_diag_sample_frame() (opt-in via NCZ_DIAG_FRAMEBUFFER)."""

import os
import pathlib
import re
import unittest

SRC = pathlib.Path(
    os.environ.get("NCZ_SRC_DIR")
    or (pathlib.Path(__file__).resolve().parents[2] / "src")
)
PERIODIC = re.compile(r"%\s*\d+\s*\)?\s*==\s*0")


class NoPeriodicReadback(unittest.TestCase):
    def test_periodic_readbacks_are_gated(self):
        offenders = []
        for path in sorted(SRC.glob("*.c")):
            lines = path.read_text(errors="replace").splitlines()
            for i, line in enumerate(lines):
                if "glReadPixels" not in line:
                    continue
                window = "\n".join(lines[max(0, i - 8) : i + 1])
                if (
                    PERIODIC.search(window)
                    and "ncz_diag_sample_frame" not in window
                    and "report_wanted" not in window
                ):
                    offenders.append("%s:%d" % (path.name, i + 1))
        self.assertEqual(offenders, [], "ungated periodic glReadPixels")

    def test_helper_defined(self):
        self.assertIn("ncz_diag_sample_frame", (SRC / "ncz_gl.h").read_text())
        self.assertIn("ncz_diag_periodic_samples", (SRC / "ncz_gl.c").read_text())


if __name__ == "__main__":
    unittest.main()
