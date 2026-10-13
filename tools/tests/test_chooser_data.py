# SPDX-License-Identifier: Apache-2.0
"""Chooser data: style categories, Black Hole simple-mode axes and simple option sets.

These are the data files the Singularity plugin draws its grouped chooser and
the Black Hole sliders from. The tests pin the invariants the UI relies on:
every shipped hack has exactly one category, every axis maps only to real
options inside their ranges, and the centre of every slider is the shipped
default so Reset is well defined.
"""

import pathlib
import re
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import chooser_data as cd

ASSETS = ROOT / "assets" / "screensaver-chooser"
OPTIONS = ASSETS / "options"


def ship_set():
    src = (ROOT / "meson.build").read_text()
    m = re.search(r"ncz_ship_bins\s*=\s*\[(.*?)\]", src, re.DOTALL)
    assert m, "ncz_ship_bins missing from meson.build"
    return re.findall(r"['\"]([A-Za-z0-9_-]+_gles3)['\"]", m.group(1))


def load_schema(path):
    rows = {}
    for line in path.read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            c = line.split("\t")
            if len(c) == 10:
                rows[c[0]] = c
    return rows


# -- categories --------------------------------------------------------------


def test_every_shipped_hack_has_exactly_one_category():
    assert cd.validate_categories(ROOT, ship_set()) == []


def test_category_list_is_small_and_ordered():
    cats = cd.load_category_list(ASSETS / "category-list.tsv")
    assert 6 <= len(cats) <= 10
    assert len({c[0] for c in cats}) == len(cats)


def test_every_category_is_used():
    cats = {c[0] for c in cd.load_category_list(ASSETS / "category-list.tsv")}
    used = {
        v["category"] for v in cd.load_categories(ASSETS / "categories.tsv").values()
    }
    assert cats == used


def test_in_house_savers_are_marked_ncz():
    cats = cd.load_categories(ASSETS / "categories.tsv")
    assert cats["blackhole_gles3"]["origin"] == "ncz"
    assert cats["fractasia_gles3"]["origin"] == "ncz"
    assert cats["xshadertoy_neonhorizon_gles3"]["origin"] == "ncz"
    assert cats["xshadertoy_neongravity_gles3"]["origin"] == "ncz"
    assert all(
        v["origin"] == "ported" for k, v in cats.items() if k.startswith("hyprsaver_")
    )


def test_validator_rejects_duplicates_missing_and_unknown(tmp_path):
    base = tmp_path / "assets" / "screensaver-chooser"
    base.mkdir(parents=True)
    (base / "category-list.tsv").write_text("a\tA\tdesc\n")
    (base / "categories.tsv").write_text("x_gles3\ta\tported\tt\nx_gles3\tb\tnope\tt\n")
    problems = cd.validate_categories(tmp_path, ["x_gles3", "y_gles3"])
    joined = "\n".join(problems)
    assert "listed twice" in joined
    assert "y_gles3" in joined  # missing
    # a second file with an unknown category and a bad origin
    (base / "categories.tsv").write_text("x_gles3\tb\tnope\tt\ny_gles3\ta\tported\t\n")
    joined = "\n".join(cd.validate_categories(tmp_path, ["x_gles3", "y_gles3"]))
    assert "unknown category" in joined
    assert "origin must be" in joined
    assert "at least one tag" in joined


def test_validator_rejects_ported_shader_marked_ncz(tmp_path):
    base = tmp_path / "assets" / "screensaver-chooser"
    base.mkdir(parents=True)
    glsl = tmp_path / "vendor" / "xshadertoy" / "glsl"
    glsl.mkdir(parents=True)
    (glsl / "foo.glsl").write_text("// Author: someone\n// URL: https://example\n")
    (base / "category-list.tsv").write_text("a\tA\tdesc\n")
    (base / "categories.tsv").write_text(
        "xshadertoy_foo_gles3\ta\tncz\tt\nhyprsaver_bar_gles3\ta\tncz\tt\n"
    )
    joined = "\n".join(
        cd.validate_categories(
            tmp_path, ["xshadertoy_foo_gles3", "hyprsaver_bar_gles3"]
        )
    )
    assert "does not say original work" in joined
    assert "is a port and cannot be marked ncz" in joined


# -- Black Hole simple mode ---------------------------------------------------


def test_blackhole_simple_axes_validate_against_schema():
    schema = load_schema(OPTIONS / "blackhole.tsv")
    assert cd.validate_simple_axes(OPTIONS / "blackhole-simple.tsv", schema) == []


def test_blackhole_simple_has_the_six_axes_in_order():
    axes = cd.load_simple_axes(OPTIONS / "blackhole-simple.tsv")
    assert [a["id"] for a in axes] == [
        "glow",
        "drama",
        "motion",
        "cosmos",
        "disk",
        "detail",
    ]


@pytest.mark.parametrize("position", [0, 25, 50, 75, 100])
def test_every_axis_stays_in_bounds_at_every_position(position):
    schema = load_schema(OPTIONS / "blackhole.tsv")
    for axis in cd.load_simple_axes(OPTIONS / "blackhole-simple.tsv"):
        for opt in axis["options"]:
            row = schema[opt["name"]]
            value = cd.axis_value(opt["curve"], opt["stops"], position)
            if row[1] in ("int", "float"):
                assert float(row[3]) <= float(value) <= float(row[4]), (
                    axis["id"],
                    opt["name"],
                    position,
                )
            elif row[1] == "enum":
                assert value in row[5].split(","), (axis["id"], opt["name"], position)


def test_centre_of_every_slider_is_the_shipped_default():
    schema = load_schema(OPTIONS / "blackhole.tsv")
    for axis in cd.load_simple_axes(OPTIONS / "blackhole-simple.tsv"):
        for opt in axis["options"]:
            row = schema[opt["name"]]
            at50 = cd.axis_value(opt["curve"], opt["stops"], 50)
            if row[1] in ("int", "float"):
                assert float(at50) == float(row[2])
            else:
                assert at50 == row[2]


def test_axis_value_and_position_round_trip():
    stops = cd.parse_stops("0=0.5,50=1,100=2")
    assert cd.axis_value("linear", stops, 0) == "0.5"
    assert cd.axis_value("linear", stops, 25) == "0.75"
    assert cd.axis_value("linear", stops, 75) == "1.5"
    assert cd.axis_value("linear", stops, 100) == "2"
    assert cd.axis_value("linear", stops, 140) == "2"  # clamped
    for pos in (0, 10, 50, 80, 100):
        assert (
            cd.axis_position("linear", stops, cd.axis_value("linear", stops, pos))
            == pos
        )
    steps = cd.parse_stops("0=low,25=medium,50=auto,75=high,100=ultra")
    assert cd.axis_value("step", steps, 49) == "medium"
    assert cd.axis_value("step", steps, 50) == "auto"
    assert cd.axis_value("step", steps, 99) == "high"
    assert cd.axis_position("step", steps, "high") == 75


def test_axis_validator_catches_bad_mappings(tmp_path):
    schema = {
        "exposure": ["exposure", "float", "1", "0.2", "4", "", "E", "d", "Look", "X"],
        "quality": [
            "quality",
            "enum",
            "auto",
            "",
            "",
            "auto,low,high",
            "Q",
            "d",
            "System",
            "Y",
        ],
    }
    p = tmp_path / "x-simple.tsv"
    p.write_text(
        "glow\t-\t-\t-\tGlow\tdesc\tDim\tBright\n"
        "glow\texposure\tlinear\t0=0.1,50=1,100=9\t\t\t\t\n"  # outside range both ends
        "glow\tnope\tlinear\t0=0,100=1\t\t\t\t\n"  # unknown option
        "detail\t-\t-\t-\tDetail\tdesc\tFast\tFine\n"
        "detail\tquality\tstep\t0=low,100=high\t\t\t\t\n"  # value at 50 is low, default auto
        "detail\texposure\tlinear\t0=0.5,50=1,100=2\t\t\t\t\n"  # option used twice
    )
    joined = "\n".join(cd.validate_simple_axes(p, schema))
    assert "outside 0.2..4" in joined
    assert "not an option of the schema" in joined
    assert "value at 50 is low" in joined
    assert "already used by axis glow" in joined


def test_axis_validator_requires_end_stops_and_monotone(tmp_path):
    schema = {
        "spin": ["spin", "float", "0", "0", "0.998", "", "S", "d", "Physics", "Z"]
    }
    p = tmp_path / "x-simple.tsv"
    p.write_text(
        "disk\t-\t-\t-\tDisk\tdesc\tA\tB\ndisk\tspin\tlinear\t10=0,50=0,100=0.9\t\t\t\t\n"
    )
    assert any(
        "stops must include 0 and 100" in s for s in cd.validate_simple_axes(p, schema)
    )
    p.write_text(
        "disk\t-\t-\t-\tDisk\tdesc\tA\tB\ndisk\tspin\tlinear\t0=0.5,50=0,100=0.9\t\t\t\t\n"
    )
    assert any("monotone" in s for s in cd.validate_simple_axes(p, schema))


# -- simple option sets -------------------------------------------------------


def test_simple_sets_name_existing_options():
    assert cd.validate_simple_sets(OPTIONS, load_schema) == []


def test_simple_sets_are_short():
    for hack, names in cd.load_simple_sets(OPTIONS / "simple.tsv").items():
        assert 1 <= len(names) <= cd.MAX_SIMPLE, hack


def test_simple_set_validator_rejects_unknown_and_too_many(tmp_path):
    (tmp_path / "foo.tsv").write_text("speed\tfloat\t1\t0\t2\t\tS\td\tG\tE\n")
    (tmp_path / "simple.tsv").write_text(
        "foo\tspeed,nope\nbar\tspeed\nfoo_gles3\t"
        + ",".join(f"o{i}" for i in range(9))
        + "\n"
    )
    joined = "\n".join(cd.validate_simple_sets(tmp_path, load_schema))
    assert "'nope' is not in options/foo.tsv" in joined
    assert "no options/bar.tsv" in joined
    assert "at most 8" in joined
