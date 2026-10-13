# SPDX-License-Identifier: Apache-2.0
"""tools/magmasimplex-realism-gates.py on synthetic frames.

Two frames are drawn: a "candy" one with the defects the operator rejected
(glossy point highlights on identical hard-edged ellipses, an even white
speckle, ring-outlined bubbles, wax darker than the liquid) and a "lamp" one
with soft-edged, irregular, luminous blobs and a clean liquid. The gates must
fail the first and pass the second, and each metric must move in the
documented direction between them.
"""

import importlib.machinery
import importlib.util
import pathlib

import numpy as np
import pytest

pytest.importorskip("scipy")
PIL = pytest.importorskip("PIL")
from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _load():
    path = ROOT / "tools" / "magmasimplex-realism-gates.py"
    loader = importlib.machinery.SourceFileLoader("mrg", str(path))
    spec = importlib.util.spec_from_loader("mrg", loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


def _blobs():
    rng = np.random.default_rng(7)
    out = []
    for i in range(7):
        cx, cy = 120 + i * 110 + rng.integers(-15, 15), 180 + rng.integers(-60, 60)
        out.append((cx, cy))
    return out


def candy_frame():
    w, h = 960, 540
    im = Image.new("RGB", (w, h), (205, 190, 215))  # pale liquid
    d = ImageDraw.Draw(im)
    for cx, cy in _blobs():
        d.ellipse(
            (cx - 38, cy - 55, cx + 38, cy + 55), fill=(225, 120, 20)
        )  # identical, hard
        d.ellipse(
            (cx - 14, cy - 36, cx - 2, cy - 22), fill=(255, 250, 235)
        )  # glossy highlight
    rng = np.random.default_rng(3)
    for _ in range(1200):  # even white speckle
        x, y = rng.integers(0, w), rng.integers(0, h)
        d.point((x, y), fill=(255, 255, 255))
    for cx, cy in [(300, 420), (700, 90), (520, 470)]:  # ring-outlined bubbles
        d.ellipse(
            (cx - 22, cy - 22, cx + 22, cy + 22), outline=(245, 245, 245), width=2
        )
    return im


def lamp_frame():
    w, h = 960, 540
    base = np.zeros((h, w, 3), np.float32)
    for y in range(h):  # liquid: vertical gradient, bright at the base
        g = 0.25 + 0.75 * (y / h)
        base[y] = (210 * g, 120 * g, 30 * g)
    im = Image.fromarray(base.astype(np.uint8))
    mask = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(mask)
    rng = np.random.default_rng(11)
    for cx, cy in _blobs():
        rx, ry = rng.integers(30, 50), rng.integers(40, 75)
        pts = []
        for k in range(40):  # irregular lump
            a = 2 * np.pi * k / 40
            r = (
                1.0
                + 0.12 * np.sin(3 * a + cx * 0.01)
                + 0.08 * np.sin(5 * a + cy * 0.02)
            )
            pts.append((cx + rx * r * np.cos(a), cy + ry * r * np.sin(a)))
        d.polygon(pts, fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(4))  # soft silhouette
    wax = Image.new("RGB", (w, h), (255, 225, 160))  # luminous pale wax
    im = Image.composite(wax, im, mask)
    return im


def test_gates_fail_candy_and_pass_lamp(tmp_path):
    mrg = _load()
    cf, lf = tmp_path / "candy.png", tmp_path / "lamp.png"
    candy_frame().save(cf)
    lamp_frame().save(lf)
    mc = mrg.measure(mrg.load(cf))
    ml = mrg.measure(mrg.load(lf))
    assert mc["n_blobs"] >= 5 and ml["n_blobs"] >= 5
    # Direction of every metric between the two looks.
    assert mc["speckle_per_mpx"] > 10 * max(ml["speckle_per_mpx"], 1.0), (mc, ml)
    assert mc["spec_per_100k"] > 0 and ml["spec_per_100k"] == 0, (mc, ml)
    assert mc["edge_rel"] < ml["edge_rel"], (mc, ml)
    assert mc["ellipse_iou"] > ml["ellipse_iou"], (mc, ml)
    assert mc["wax_over_liquid"] < 1.0 < ml["wax_over_liquid"], (mc, ml)
    assert mc["thin_bright"] > ml["thin_bright"], (mc, ml)
    # The default thresholds (from the reference frames) separate them.
    candy_ok = all(ok for _, ok, _, _ in mrg.gate(mc))
    lamp_ok = all(ok for _, ok, _, _ in mrg.gate(ml))
    assert not candy_ok
    assert lamp_ok, mrg.gate(ml)


def test_cli_gate_exit_codes(tmp_path):
    import subprocess
    import sys

    cf, lf = tmp_path / "candy.png", tmp_path / "lamp.png"
    candy_frame().save(cf)
    lamp_frame().save(lf)
    tool = ROOT / "tools" / "magmasimplex-realism-gates.py"
    bad = subprocess.run(
        [sys.executable, str(tool), "gate", str(cf)],
        capture_output=True,
        text=True,
        check=False,
    )
    good = subprocess.run(
        [sys.executable, str(tool), "gate", str(lf)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert bad.returncode == 1 and "FAIL" in bad.stdout
    assert good.returncode == 0 and "FAIL" not in good.stdout
