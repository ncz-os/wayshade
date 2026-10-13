# SPDX-License-Identifier: Apache-2.0
import importlib.machinery
import importlib.util
import unittest
from pathlib import Path

L = Path(__file__).resolve().parents[1] / "ncz-screensaver"
_l = importlib.machinery.SourceFileLoader("ncz_screensaver_mig", str(L))
ns = importlib.util.module_from_spec(
    importlib.util.spec_from_loader("ncz_screensaver_mig", _l)
)
_l.exec_module(ns)


class MigrateIds(unittest.TestCase):
    def test_old_ids_map_to_new(self):
        v = ns.migrate_ids(
            {
                "hack-id": "gibson_gles3",
                "random-hacks": ["hyprsaver_tesla_gles3", "voronoi_gles3"],
                "playlist": ["hyprsaver_matrix_gles3"],
                "hack-options": {"hyprsaver_stonks_gles3": {"speed": "2"}},
            }
        )
        self.assertEqual(v["hack-id"], "datatowers_gles3")
        self.assertEqual(
            v["random-hacks"], ["hyprsaver_arccoil_gles3", "voronoi_gles3"]
        )
        self.assertEqual(v["playlist"], ["hyprsaver_digitalrain_gles3"])
        self.assertEqual(v["hack-options"], {"hyprsaver_ticker_gles3": {"speed": "2"}})

    def test_current_ids_untouched(self):
        v = {
            "hack-id": "blackhole_gles3",
            "random-hacks": [],
            "playlist": [],
            "hack-options": {},
        }
        self.assertEqual(ns.migrate_ids(dict(v)), v)


if __name__ == "__main__":
    unittest.main()
