# SPDX-License-Identifier: Apache-2.0
"""Both black hole jet poles must stream OUTWARD from the hole.

Regression for the bug where the knot phase in ``jet_glow`` carried a
``sign(sa)`` factor.  ``h`` is the unsigned distance along the jet axis
(``abs(sa)``), so multiplying the time term by ``sign(sa)`` inverted ``dh/dt``
on the negative pole: one jet ran inward, the other outward, and on screen both
appeared to travel the same way.

These checks drive the *actual* phase expression parsed out of the shader
source, so they fail if the sign ever comes back.  The GPU-side counterpart
(offscreen EGL/GLES3 capture of the real shader) lives in
``tools/jet-validation/``.
"""

import pathlib
import re
import math
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]

FRAG = ROOT / "vendor" / "blackhole" / "blackhole.frag"

# Verbatim bad-phase fixture from the public pre-fix shader, commit 03fd4d49
# (vendor/blackhole/blackhole.frag, jet_glow "float knots=" line):
#     float knots=.55+.45*noise(vec2(h*.55-u_time*1.6*sign(sa), ...));
# Only the first noise() argument matters here, and it is embedded literally so
# this suite runs from a bare export with no .git directory and no private
# objects.  The GPU-side proof against the real old binary stays upstream.
PRE_FIX_PHASE_EXPR = "h*.55-u_time*1.6*sign(sa)"
PRE_FIX_SOURCE_COMMIT = "03fd4d49"


def _knots_phase_expr() -> str:
    """First argument of the noise() call inside jet_glow's knots line."""
    body = FRAG.read_text()
    body = body[body.index("vec3 jet_glow(") :]
    line = next(l for l in body.splitlines() if l.strip().startswith("float knots="))
    return line[line.index("noise(vec2(") + len("noise(vec2(") : line.index(",atan")]


def _phase(expr: str, h: float, t: float, sa: float) -> float:
    """Evaluate the shader phase expression with h, u_time and sa substituted."""
    py = expr.replace("u_time", repr(float(t))).replace("h", repr(float(h)))
    py = py.replace("sa", repr(float(sa)))
    # GLSL abs/absf map onto Python; nothing else in the expression may be free.
    py = py.replace("absf(", "abs(")
    if re.search(r"[A-Za-z_]", re.sub(r"\b(abs|sign)\b", "", py)):
        raise AssertionError("unhandled identifier in phase expression: %r" % expr)
    return eval(  # noqa: S307 - fixed input, no free identifiers
        py,
        {"__builtins__": {}},
        {"abs": abs, "sign": lambda x: math.copysign(1.0, x)},
    )


def _dh_dt(expr: str, sa: float, t: float) -> float:
    """Signed rate of change of the knot phase with respect to phase-time.

    A knot sits where the phase is stationary, i.e. where dphase/dh = 0, so
    the sign of -dphase/dt tracks whether the knot marches along increasing h
    (away from the hole) or back toward it.  It is a phase-time rate in the
    shader's units, not a world-space speed.
    """
    h = abs(sa)
    dt = 1e-3
    return -(_phase(expr, h, t + dt, sa) - _phase(expr, h, t, sa)) / dt


def test_knot_phase_has_no_pole_sign():
    expr = _knots_phase_expr()
    assert "sign(" not in expr, expr
    # time must still advance the phase: knots must actually animate.
    assert "u_time" in expr, expr


def test_both_poles_phase_outward():
    expr = _knots_phase_expr()
    # Positive knot drift speed toward increasing h, on both poles.
    for t in (0.0, 3.7, 41.25):
        assert _dh_dt(expr, +20.0, t) > 0.0, (expr, "positive pole", t)
        assert _dh_dt(expr, -20.0, t) > 0.0, (expr, "negative pole", t)
    # ...and the two poles drift in the same radial direction (both outward).
    assert abs(_dh_dt(expr, +20.0, 5.0) - _dh_dt(expr, -20.0, 5.0)) < 1e-9


def test_pre_fix_phase_fixture_fails_the_same_check():
    """Negative control: the historical bad phase must trip this check.

    The fixture is a literal transcription of the public pre-fix shader
    (03fd4d49); when that commit happens to be present locally it is
    re-verified byte-for-byte, but the test never *requires* a git repo.
    """
    old = PRE_FIX_PHASE_EXPR
    assert "sign(sa)" in old, old
    assert _dh_dt(old, -20.0, 5.0) < 0.0 < _dh_dt(old, +20.0, 5.0), old


def test_phase_fixture_matches_public_pre_fix_shader():
    """Cross-check the literal fixture against the public pre-fix commit.

    Skipped outside a git checkout; the negative control above never depends on
    this, so the exported suite still runs from a bare tree.
    """
    try:
        base = subprocess.run(
            ["git", "-C", str(ROOT), "show",
             "%s:vendor/blackhole/blackhole.frag" % PRE_FIX_SOURCE_COMMIT],
            capture_output=True, text=True, check=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError):
        pytest.skip("public pre-fix commit not available in this checkout")
    body = base[base.index("vec3 jet_glow("):]
    line = next(l for l in body.splitlines() if l.strip().startswith("float knots="))
    expr = line[line.index("noise(vec2(") + len("noise(vec2("):line.index(",atan")]
    assert expr == PRE_FIX_PHASE_EXPR, expr


def test_jet_axis_is_free_to_rotate():
    """A non-world-up axis must not reintroduce a fixed world direction.

    The phase depends only on the axial coordinate h, never on ray.x/ray.y/ray.z,
    so a tilted axis (e.g. normalize(vec3(.6,.8,0))) keeps both poles outward.
    """
    expr = _knots_phase_expr()
    assert not re.search(r"\b(ray|cam|axis|gl_FragCoord|q)\b", expr), expr
    body = FRAG.read_text()
    body = body[body.index("vec3 jet_glow(") : body.index("float knots=")]
    assert "axis0" in body  # axis comes from the caller, per frame