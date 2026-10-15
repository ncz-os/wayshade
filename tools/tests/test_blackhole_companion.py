# SPDX-License-Identifier: Apache-2.0
"""Black Hole companion star: off unless the user opts in."""

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
PRESETS = ROOT / "assets" / "screensaver-chooser" / "options" / "presets" / "blackhole"
SCHEMA = ROOT / "assets" / "screensaver-chooser" / "options" / "blackhole.tsv"


def test_companion_option_defaults_off():
    for line in SCHEMA.read_text().splitlines():
        c = line.split("\t")
        if c and c[0] == "companion":
            assert float(c[2]) == 0.0
            return
    raise AssertionError("companion option missing")


def test_no_preset_enables_companion():
    for conf in sorted(PRESETS.glob("*.conf")):
        m = re.search(r"^companion=(\S+)", conf.read_text(), re.M)
        if m:
            assert float(m.group(1)) == 0.0, conf.name


FRAG = (ROOT / "vendor" / "blackhole" / "blackhole.frag").read_text()


def test_companion_is_traced_on_the_bent_ray():
    # the star and stream are tested on each geodesic step (old -> pos), not along the
    # straight camera ray, and only when the option is on
    assert "companionAlong" not in FRAG
    loop = FRAG[FRAG.index("for(int i=0;i<int(u_max_steps);i++)") :]
    assert "if(u_comp>0.&&compSeg(old,pos,color,trans))break;" in loop
    assert "if(u_comp>0.)compInit(cam,diskN);" in FRAG


def test_companion_is_occluded_through_transmittance():
    body = FRAG[FRAG.index("bool compSeg(") : FRAG.index("// Sky/bloom constants")]
    # the star is added through the running transmittance and blocks the ray
    assert "color+=trans*" in body and "trans*=" in body and "return true" in body


def test_companion_is_deterministic():
    init = FRAG[FRAG.index("void compInit(") : FRAG.index("bool compSeg(")]
    assert "u_time" in init and "u_seed" in init
    for forbidden in ("texture(", "random(", "rand("):
        assert forbidden not in init


def test_stream_has_no_discrete_blobs():
    body = FRAG[FRAG.index("bool compSeg(") : FRAG.index("// Sky/bloom constants")]
    # a continuous tube evaluated by distance to the path with a prefilter, not 10 beads
    assert "box" in body and "dperp" in body
    assert "for(int i=1;i<=10;i++)" not in body
