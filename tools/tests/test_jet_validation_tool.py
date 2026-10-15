# SPDX-License-Identifier: Apache-2.0
"""Source-level checks for the jet-validation capture tool (no GPU needed).

The actual GL run happens on the render host; these tests keep the tool honest:
it must build the REAL shader with jet_glow() untouched, must not reimplement
any jet maths in Python, and must fail closed on a broken shader.
"""

import importlib.util
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "jet-validation" / "jet_outward_proof.py"
FRAG = ROOT / "vendor" / "blackhole" / "blackhole.frag"

spec = importlib.util.spec_from_file_location("jet_outward_proof", TOOL)
assert spec and spec.loader
tool = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = tool
spec.loader.exec_module(tool)


def test_tool_keeps_jet_glow_byte_identical():
    src = FRAG.read_text()
    out = tool.build_shader_source(FRAG)
    fn_start = src.index("vec3 jet_glow(")
    fn_end = src.index("// Zero velocity and acceleration")
    body = src[fn_start:fn_end]
    assert body in out, "jet_glow() was altered by the diagnostic wrapper"
    assert "vec2(h*.55-u_time*1.6," in out


def test_only_the_scene_main_is_renamed():
    out = tool.build_shader_source(FRAG)
    assert out.count("void main(") == 1  # exactly the appended diagnostic main
    assert "void bh_scene_main(" in out
    assert out.rstrip().endswith("}")


def test_tool_reimplements_no_jet_maths():
    text = TOOL.read_text()
    # Strip the human-readable metadata strings: only executable lines matter.
    code = [ln for ln in text.splitlines()
            if not ln.lstrip().startswith(('"', "#"))]
    body = "\n".join(code)
    for forbidden in ("atan(q", "exp(-dot", "jet_tint", "sign(sa)", ".55*noise"):
        assert forbidden not in body, forbidden
    # The only maths the tool does is camera geometry and sampling.
    assert "def camera_for_axis(" in text and "def profile(" in text


def test_build_fails_closed_on_a_second_main():
    bad = ROOT / "vendor" / "blackhole" / "blackhole.frag"
    src = bad.read_text().replace("vec3 jet_glow(", "void main(){\n}\nvec3 jet_glow(", 1)
    tmp = ROOT / "vendor" / "blackhole" / ".jet-proof-bad.frag"
    try:
        tmp.write_text(src)
        with pytest.raises(SystemExit):
            tool.build_shader_source(tmp)
    finally:
        tmp.unlink(missing_ok=True)


def test_measurement_uses_both_poles_and_two_times():
    text = TOOL.read_text()
    assert "T0, T1 = 0.0, 2.0" in text
    assert '("positive", +1.0), ("negative", -1.0)' in text
    assert "world_up" in text and "tilted" in text
    assert "all_poles_outward" in text
    assert "1.6 / 0.55" in text  # expected drift, cross-checked not hardcoded


def test_h_samples_are_one_pixel_apart_in_world_units():
    """The old 2.0-world spacing with a sample-index shift overstated drift 2x."""
    h = tool.H_SAMPLES
    assert h[0] >= 8.0 - 1e-9 and h[-1] <= 54.0 + 1e-9
    for a, b in zip(h, h[1:]):
        assert abs((b - a) - tool.SCALE) < 1e-9, (a, b)


def _translate(profile, shift):
    """Translate a profile by `shift` samples, zero-filling outside."""
    out = [0.0] * len(profile)
    for i, v in enumerate(profile):
        j = i + shift
        if 0 <= j < len(out):
            out[j] = v
    return out


def _synthetic(n=93):
    """Deterministic knotted signal with strong, non-DC contrast."""
    return [0.5 + 0.5 * ((i * 7) % 11 - 5) / 5.0 for i in range(n)]


def test_synthetic_translated_signal_measures_expected_world_shift():
    """Control for the MEASUREMENT implementation and units, not for the
    shader: a signal translated by a known world shift must be measured back
    as that shift in world units."""
    expected_world = (1.6 / 0.55) * (tool.T1 - tool.T0)
    shift = int(round(expected_world / tool.SCALE))
    p0 = _synthetic()
    p1 = _translate(p0, shift)
    s, r, n, ok, why = tool.measure_shift(p0, p1)
    assert ok, why
    assert abs(s * tool.SCALE - expected_world) <= tool.SHIFT_TOL_WORLD
    assert r > 0.9
    # sign convention: outward positive, inward negative
    s_neg, r_neg, _, ok_neg, why_neg = tool.measure_shift(p1, p0)
    assert ok_neg, why_neg
    assert s_neg * tool.SCALE < 0 and abs(s_neg * tool.SCALE + expected_world) <= tool.SHIFT_TOL_WORLD


def test_flat_and_constant_profiles_are_reported_unmeasured():
    """Old uncentred dot product passed on zeros/noise; Pearson + contrast gate
    must refuse."""
    for bad in ([0.0] * 93, [0.5] * 93):
        s, r, n, ok, why = tool.measure_shift(bad, bad)
        assert not ok, why
        assert s is None


def test_search_window_is_bounded_and_physical():
    assert abs(tool.SEARCH_WORLD - 8.0) < 1e-9
    assert tool.SEARCH_SAMPLES == int(round(8.0 / tool.SCALE))
    # A shift beyond the window must NOT be reported as if it were found.
    p0 = _synthetic()
    s, r, n, ok, why = tool.measure_shift(p0, _translate(p0, tool.SEARCH_SAMPLES + 6))
    assert s is None or abs(s) <= tool.SEARCH_SAMPLES
    assert abs(expected_shift()) - tool.EXPECTED_WORLD < 1e-9


def expected_shift():
    return (1.6 / 0.55) * (tool.T1 - tool.T0)


def test_expect_outward_requires_signal_sign_and_magnitude():
    text = TOOL.read_text()
    assert "pass_expect_outward" in text
    assert "SHIFT_TOL_WORLD" in text
    assert "DIAGNOSTIC FAILURE" in text


def test_shift_sign_convention_is_outward_positive():
    # A profile that moves toward larger index (larger h) must report positive.
    n = 60
    p0 = [0.0] * n
    p1 = [0.0] * n
    for i in range(6):
        p0[20 + i] = 1.0
        p1[26 + i] = 1.0
    s, score = tool.shift_px(p0, p1)
    assert s == 6 and score > 0
    p0, p1 = p1, p0
    s2, _ = tool.shift_px(p0, p1)
    assert s2 == -6