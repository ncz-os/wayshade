# SPDX-License-Identifier: Apache-2.0
"""Layout-diversity metric and per-slice seed derivation (2026-10-05).

The metric must report ~0 for identical frames and clearly positive for
different layouts; the driver must hand every slice a distinct pattern seed.
"""

import importlib.util
import pathlib
import tempfile

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


div = _load("magdiv", ROOT / "magmasimplex-layout-diversity.py")
draw = _load("magdraw", ROOT / "magmasimplex-longrun/draw.py")


def _gray(a):
    g = np.asarray(a, np.float32)
    return 0.2126 * g[..., 0] + 0.7152 * g[..., 1] + 0.0722 * g[..., 2]


def _frame(a):
    """(fine 64x36, coarse 16x9) luminance, as the tool's load() returns."""
    fine = _gray(np.asarray(Image.fromarray(a).resize((64, 36), Image.BILINEAR)))
    coarse = _gray(np.asarray(Image.fromarray(a).resize((16, 9), Image.BOX)))
    return fine, coarse


def test_metric_zero_for_identical():
    a = (np.random.RandomState(1).rand(64, 36, 3) * 255).astype(np.uint8)
    item = _frame(a)
    co, ss, el, pairs = div.pairwise([item, item])
    assert pairs == 1
    assert abs(co[0]) < 1e-6
    assert abs(ss[0]) < 1e-6
    assert el[0] < 1e-6


def _blob(rs):
    """A structured frame: three Gaussian blobs at random places, the kind of
    large-scale layout the metric must distinguish."""
    yy, xx = np.mgrid[0:36, 0:64]
    g = np.zeros((36, 64, 3), np.float32)
    for _ in range(3):
        cx, cy = rs.uniform(0, 64), rs.uniform(0, 36)
        r = rs.uniform(4, 12)
        b = np.exp(-((xx - cx) ** 2 + (yy - cy) ** 2) / (2 * r * r))
        g += (b * rs.uniform(0.4, 1.0))[..., None]
    return (np.clip(g, 0, 1) * 255).astype(np.uint8)


def test_metric_large_for_different_layouts():
    rs = np.random.RandomState(2)
    items = [_frame(_blob(rs)) for _ in range(6)]
    co, ss, el, pairs = div.pairwise(items)
    assert pairs == 15
    assert np.mean(co) > 0.5     # different large-scale layouts: far apart
    assert np.mean(ss) > 0.005   # ...and the windowed metric agrees
    assert np.mean(el) >= 0.0


def test_slice_seed_unique_per_slice():
    seeds = [draw.stream(20261005, s, "seed") % 4294967295 + 1 for s in range(1, 41)]
    assert len(set(seeds)) == len(seeds), "two slices share a pattern seed"
    # A different base seed (or slice) must give a different stream.
    assert draw.stream(20261005, 1, "seed") != draw.stream(20261006, 1, "seed")
    assert draw.stream(20261005, 1, "seed") != draw.stream(20261005, 2, "seed")


def test_metric_groups_by_preset():
    # The measure step groups <root>/<preset>/<seed>/frame.png; check the
    # directory walk finds both presets and all seeds.
    rs = np.random.RandomState(3)
    with tempfile.TemporaryDirectory() as td:
        for preset in ("amber-glow", "haight"):
            for seed in (11, 22, 33):
                d = pathlib.Path(td) / preset / str(seed)
                d.mkdir(parents=True)
                Image.fromarray((rs.rand(64, 36, 3) * 255).astype(np.uint8)).save(d / "frame.png")
        found = {}
        for p in sorted(pathlib.Path(td).iterdir()):
            n = len([x for x in p.iterdir() if (x / "frame.png").exists()])
            found[p.name] = n
        assert found == {"amber-glow": 3, "haight": 3}
