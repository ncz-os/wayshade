# SPDX-License-Identifier: Apache-2.0
"""In-house neongravity supersession: the shipped neongravity is the NCZ scene.

0.8.9 corrected the record because the shipped binary was still the third-party
``xshadertoy_neongravity-1`` port. These tests pin the fixed state: the in-house
scene is the shipped id, the third-party port is not, the catalogue, tiers and
provenance agree, and the 74-entry ship floor is unchanged.
"""

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSETS = ROOT / "assets" / "screensaver-chooser"

SHIPPED = "xshadertoy_neongravity_gles3"
PORT = "xshadertoy_neongravity-1_gles3"


def ship_set():
    src = (ROOT / "meson.build").read_text()
    m = re.search(r"ncz_ship_bins\s*=\s*\[(.*?)\]", src, re.DOTALL)
    assert m, "ncz_ship_bins missing from meson.build"
    return re.findall(r"['\"]([A-Za-z0-9_-]+_gles3)['\"]", m.group(1))


def _rows(path):
    return [
        line.split("\t")
        for line in path.read_text().splitlines()
        if line.strip() and not line.startswith("#")
    ]


def _by_id(path):
    return {row[0]: row for row in _rows(path)}


def test_ship_set_has_the_inhouse_and_hits_the_74_floor():
    ships = ship_set()
    assert len(ships) == 74
    assert SHIPPED in ships
    assert PORT not in ships


def test_category_row_is_the_inhouse_scene_marked_ncz():
    rows = _by_id(ASSETS / "categories.tsv")
    assert SHIPPED in rows
    assert PORT not in rows
    assert rows[SHIPPED][2] == "ncz"


def test_tiers_row_present_for_inhouse_and_absent_for_port():
    ids = {row[0] for row in _rows(ASSETS / "tiers.tsv")}
    assert SHIPPED in ids
    assert PORT not in ids


def test_provenance_records_the_landed_supersession():
    # The keys are the exact first-column ids in docs/PROVENANCE.tsv, which
    # lists saver rows with _gles3 and shader rows without _gles3 on purpose.
    # universeball and gravitywell were cut from the ship set by the 0.8.2
    # cohort, whose changelog already stated the reason as "displaced by the
    # neongravity scene". Until 2026-10-05 the shipped neongravity was the
    # third-party port, so 0.8.9 had to record the correction; now that the
    # in-house scene ships, the displacer named in the original reason is the
    # in-house id and the label is truthful.
    rows = _by_id(ROOT / "docs" / "PROVENANCE.tsv")
    assert rows["xshadertoy_neongravity"][6] == "shipped"
    assert rows[PORT][6] == "displaced-by:xshadertoy_neongravity"
    assert rows["xshadertoy_universeball"][6] == "displaced-by:xshadertoy_neongravity"
    assert rows["gravitywell_gles3"][6] == "displaced-by:xshadertoy_neongravity"


def test_presets_listed_for_the_shipped_scene():
    text = (ASSETS / "presets.tsv").read_text()
    for pid in ("quiet-well", "swarm", "tight-spiral", "wide-horizon"):
        assert f"{SHIPPED}--{pid}\t" in text
    assert PORT not in text
