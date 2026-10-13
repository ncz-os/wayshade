# SPDX-License-Identifier: Apache-2.0
import importlib.machinery
import importlib.util
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

L = Path(__file__).resolve().parents[1] / "ncz-screensaver"
_l = importlib.machinery.SourceFileLoader("ncz_screensaver_dropin", str(L))
ns = importlib.util.module_from_spec(
    importlib.util.spec_from_loader("ncz_screensaver_dropin", _l)
)
_l.exec_module(ns)


class DropIns(unittest.TestCase):
    def test_catalog_and_tiers_read_dropins(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d / "hacks.tsv").write_text("a_gles3\tA\tg\tg\n")
            (d / "hacks.d").mkdir()
            (d / "hacks.d" / "extra.tsv").write_text("b_gles3\tB\tx\tx\n")
            (d / "tiers.tsv").write_text("a_gles3\tweak\t-\t-\t-\t-\t-\td c\n")
            (d / "tiers.d").mkdir()
            (d / "tiers.d" / "extra.tsv").write_text(
                "b_gles3\tmid\t-\t-\t-\t-\t-\td c\n"
            )
            env = {
                "NCZ_SCREENSAVER_CATALOG": str(d / "hacks.tsv"),
                "NCZ_SCREENSAVER_TIERS": str(d / "tiers.tsv"),
            }
            with mock.patch.dict(os.environ, env):
                self.assertEqual(
                    [r["id"] for r in ns.load_catalog()], ["a_gles3", "b_gles3"]
                )
                self.assertEqual(ns.load_tiers().get("b_gles3"), "mid")

    def test_no_dropin_dir(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "hacks.tsv"
            p.write_text("a_gles3\tA\tg\tg\n")
            self.assertEqual(ns.with_dropins(p), [p])


if __name__ == "__main__":
    unittest.main()
