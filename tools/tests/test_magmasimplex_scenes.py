# SPDX-License-Identifier: Apache-2.0
"""magmasimplex scenes: the renamed Flowerchild set and its old-id aliases.

The operator named the Flowerchild scenes on 2026-10-05; the earlier
`butterfly-*` ids must keep resolving (saved settings), through `.conf` alias
files that the schema tool generates and the binary's plain `<id>.conf` lookup
finds. These tests need no binary: they check the tool's table and the files
on disk.
"""

import importlib.machinery
import importlib.util
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
PRESETS = ROOT / "assets/screensaver-chooser/options/presets/magmasimplex"

NAMED = {
    "conspiracy": "Conspiracy",
    "grape": "Grape",
    "haight": "Haight",
    "airplane": "Airplane",
    "prune": "Prune",
    "butterfly": "Butterfly",
    "fillmore": "Fillmore",
    "strawberry": "Strawberry",
    "elevator": "Elevator",
    "silver-apple": "Silver Apple",
    "technicolor": "Technicolor",
    "ufo": "UFO",
    "middle-earth": "Middle Earth",
    "roundhouse": "Roundhouse",
    "hapshash": "Hapshash",
    "kaleidoscope": "Kaleidoscope",
    "piper": "Piper",
    "itchycoo": "Itchycoo",
    "carnaby": "Carnaby",
    "inkwell": "Inkwell",
    "veins": "Veins",
    "avalon": "Avalon",
    "fillmore-small": "Fillmore small",
}
HAIGHT_ASHBURY = {
    "haight",
    "fillmore",
    "strawberry",
    "conspiracy",
    "inkwell",
    "veins",
    "avalon",
    "fillmore-small",
}
SWINGING_LONDON = {
    "technicolor",
    "ufo",
    "middle-earth",
    "roundhouse",
    "hapshash",
    "kaleidoscope",
    "piper",
    "itchycoo",
    "carnaby",
}


def _tool():
    path = ROOT / "tools" / "magmasimplex-schema.py"
    loader = importlib.machinery.SourceFileLoader("mss", str(path))
    spec = importlib.util.spec_from_loader("mss", loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


def _conf(path):
    """[preset] and [magmasimplex] keys of a scene key file."""
    out = {"preset": {}, "magmasimplex": {}, "comments": []}
    sec = None
    for line in path.read_text().splitlines():
        if not line.strip():
            continue
        if line.startswith("#"):
            out["comments"].append(line)
            continue
        if line.startswith("["):
            sec = line.strip("[]")
            continue
        k, _, v = line.partition("=")
        if sec in out:
            out[sec][k] = v
    return out


def test_named_scenes_exist_with_display_names():
    mod = _tool()
    for sid, title in NAMED.items():
        assert sid in mod.SCENES, sid
        assert mod.scene(sid)[0] == title
        assert mod.scene(sid)[2]["view"] == "butterfly"
        assert (PRESETS / f"{sid}.conf").exists() and (PRESETS / f"{sid}.json").exists()
    for sid in HAIGHT_ASHBURY:
        assert mod.SCENES[sid][1].startswith("Haight-Ashbury:"), sid
    for sid in SWINGING_LONDON:
        assert mod.SCENES[sid][1].startswith("Swinging London:"), sid
    assert mod.SCENES["roundhouse"][2]["projector-mask"] == "disc"
    # roundhouse keeps its old disc radius now that disc defaults to the large
    # size; the disc/preset keep their look.
    assert mod.SCENES["roundhouse"][2]["projector-mask-size"] == "0.80"
    assert mod.SCENES["hapshash"][2]["poster"] == "true"
    for sid in NAMED:
        cls = mod.scene(sid)[3]
        assert cls in ("weak", "mid"), (sid, cls)
        assert (PRESETS / f"{sid}.json").read_text().count(f'"min_class": "{cls}"') == 1
    # The liked red/yellow look keeps its soft projector mask.
    assert mod.SCENES["haight"][2]["projector-mask"] == "soft"
    assert mod.SCENES["fillmore"][2]["projector-mask"] == "dish"
    assert mod.SCENES["elevator"][2]["overlay"] == "both"


def test_dendrite_presets_and_mask_sizes():
    """The dendrite layer's three presets, and the fillmore size migration."""
    mod = _tool()
    # The three new presets all use the layer, so they are mid, not weak.
    assert mod.scene("inkwell")[3] == "mid"
    assert mod.scene("veins")[3] == "mid"
    assert mod.scene("avalon")[3] == "mid"
    assert mod.SCENES["inkwell"][2]["dendrites"] == "ink"
    assert mod.SCENES["veins"][2]["dendrites"] == "veins"
    assert mod.SCENES["avalon"][2]["dendrites"] == "ink"
    assert mod.SCENES["inkwell"][2]["projector-mask"] == "dish"
    # fillmore now uses the style's new large default (key omitted); the old
    # small dish is fillmore-small at the operator's recorded 0.47.
    assert "projector-mask-size" not in mod.SCENES["fillmore"][2]
    assert mod.SCENES["fillmore-small"][2]["projector-mask-size"] == "0.47"
    assert mod.scene("fillmore-small")[3] == "weak"
    # No existing lamp preset gained a dendrite layer.
    for sid in mod.SCENES:
        if sid in ("inkwell", "veins", "avalon"):
            continue
        assert "dendrites" not in mod.scene(sid)[2], sid


def test_old_butterfly_ids_resolve_as_aliases():
    mod = _tool()
    old_ids = {
        "butterfly-sixties",
        "butterfly-royal",
        "butterfly-sunburst",
        "butterfly-holographic",
        "butterfly-ember",
        "butterfly-neon-pulse",
        "butterfly-fillmore",
        "butterfly-crimson",
    }
    assert set(mod.ALIASES) == old_ids
    for old, new in mod.ALIASES.items():
        assert new in mod.SCENES, (old, new)
        alias = PRESETS / f"{old}.conf"
        assert alias.exists(), alias
        # An alias is a .conf only: the chooser must not list the scene twice.
        assert not (PRESETS / f"{old}.json").exists()
        a, b = _conf(alias), _conf(PRESETS / f"{new}.conf")
        assert a["magmasimplex"] == b["magmasimplex"], old
        assert a["preset"]["name"] == b["preset"]["name"] == mod.scene(new)[0]
        assert any("alias" in c for c in a["comments"])


def test_no_band_names_in_scene_text():
    """The names nod at 1960s bands; the band names themselves never appear."""
    mod = _tool()
    banned = (
        "jefferson",
        "iron butterfly",
        "grateful",
        "strawberry alarm",
        "13th floor",
        "silver apples",
        "moby",
        "pink floyd",
        "small faces",
        "soft machine",
    )
    for sid in mod.SCENES:
        title, desc = mod.scene(sid)[:2]
        text = (title + " " + desc).lower()
        for b in banned:
            assert b not in text, (sid, b)
