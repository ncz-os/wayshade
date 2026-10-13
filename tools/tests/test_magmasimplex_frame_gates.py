# SPDX-License-Identifier: Apache-2.0
"""tools/magmasimplex-frame-gates.py: the dendrite and coverage metrics.

Two synthetic frames are drawn: a smooth lamp-like field (a soft radial glow,
no fine structure) and a branching ink-tree frame (many thin radial lines on a
bright dish, plus a red droplet). The dendrite edge metric must be low for the
first and high for the second, and coverage must track the non-background share
of the frame. This is the unit-level companion to the frame captures on the
devices (see report.md / the evidence dir).
"""

import importlib.machinery
import importlib.util
import pathlib

import numpy as np
import pytest

pytest.importorskip("PIL")
from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _load():
    path = ROOT / "tools" / "magmasimplex-frame-gates.py"
    loader = importlib.machinery.SourceFileLoader("mfg", str(path))
    spec = importlib.util.spec_from_loader("mfg", loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


def smooth_frame():
    """A lamp-like field: a soft warm gradient, no thin structure."""
    w, h = 960, 540
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    r = np.hypot(x - w / 2, (y - h) * 0.8) / (0.75 * h)
    a = np.clip(1.2 - r, 0.0, 1.0)
    img = np.stack(
        [40 + 200 * a, 30 + 150 * a, 20 + 60 * a], axis=-1
    ).astype(np.uint8)
    im = Image.fromarray(img, "RGB").filter(ImageFilter.GaussianBlur(6))
    return np.asarray(im).astype(np.float32) / 255.0


def branch_frame():
    """An ink tree: thin dark fingers radiating from a centre on a bright dish."""
    w, h = 960, 540
    im = Image.new("RGB", (w, h), (10, 9, 11))
    d = ImageDraw.Draw(im)
    cx, cy = w // 2, h // 2
    d.ellipse((cx - 300, cy - 270, cx + 300, cy + 270), fill=(250, 244, 232))
    rng = np.random.default_rng(5)
    for k in range(26):
        ang = 2 * np.pi * k / 26 + rng.uniform(-0.05, 0.05)
        x, y = cx, cy
        for step in range(9):
            ln = rng.uniform(14, 30)
            x2, y2 = x + ln * np.cos(ang), y + ln * np.sin(ang)
            d.line((x, y, x2, y2), fill=(12, 10, 14), width=rng.integers(1, 3))
            ang += rng.uniform(-0.7, 0.7)
            x, y = x2, y2
    return np.asarray(im).astype(np.float32) / 255.0


def lit_frame():
    """Mostly non-background (a full bright field)."""
    w, h = 960, 540
    return np.full((h, w, 3), 0.7, np.float32)


def dark_frame():
    """Mostly background (near black)."""
    return np.full((540, 960, 3), 0.02, np.float32)


def test_dendrite_metric_is_low_smooth_high_branching():
    mod = _load()
    smooth = mod.dendrite_metrics(smooth_frame())
    branch = mod.dendrite_metrics(branch_frame())
    # edges: thin high-contrast fingers vs a soft gradient.
    assert smooth[0] < 0.02, smooth
    assert branch[0] > 0.02, branch
    assert branch[0] > 3 * smooth[0], (smooth, branch)
    # the tree's edges run in every direction; the soft glow has almost none.
    assert branch[1] > smooth[1]


def test_coverage_tracks_non_background_share():
    mod = _load()
    assert mod.dendrite_metrics(lit_frame())[2] > 0.95
    assert mod.dendrite_metrics(dark_frame())[2] < 0.05
    # the branching frame sits on a bright dish, so most of it is covered.
    assert mod.dendrite_metrics(branch_frame())[2] > 0.4
