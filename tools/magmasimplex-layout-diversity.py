#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""magmasimplex-layout-diversity.py - does the pattern change with the seed?

The operator (2026-10-05) saw the same Flowerchild marbling and the same Lava
arrangement at different palettes: the seed changed colour, not layout. This
tool measures layout, not colour:

  * render mode (--render BIN): for each (preset, seed) run the saver with
    NCZ_FRAME_DUMP, keep the first frame at/after frame 60 (same warm-up and so
    the same slow drift for every seed), and store it under
    <out>/<preset>/<seed>/frame.png;
  * measure mode (default): for each preset load one frame per seed and
    compute, over all seed pairs,
      corr_dist  mean 1 - Pearson correlation of the 16x9 luminance (BOX), the
                 large-scale layout. A fixed field gives ~0; a seed-drawn field
                 gives a large value even when colour changes (colour is
                 normalised out by the correlation).
      ssim_dist  mean 1 - windowed SSIM (8x8 blocks) at 64x36.
      edge_l1    mean L1 distance of the Sobel edge maps at 64x36, /255.

  PASS when a preset's mean corr_dist is at least --threshold (default 0.25)
  and every pair clears --pair-threshold (default 0.02), so no two seeds still
  land on the same layout.

usage:
  magmasimplex-layout-diversity.py OUTDIR                 # measure
  magmasimplex-layout-diversity.py OUTDIR --render BIN \
      --preset haight --seeds 101 202 303 ... [--seconds 4]

Environment for --render (Wayland/EGL) is inherited; run the tool with the
same XDG_RUNTIME_DIR/WAYLAND_DISPLAY the saver needs. The binary is executed
with --cwd as its working directory (the source/build tree root) so its
shader loader finds vendor/magmasimplex/*.frag.
"""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

W, H = 64, 36           # SSIM / edge resolution
CW, CH = 16, 9          # coarse layout resolution
WARM_FRAME = 60         # first dump at/after this frame number (hex names)


def gray(y):
    return 0.2126 * y[..., 0] + 0.7152 * y[..., 1] + 0.0722 * y[..., 2]


def load(path):
    im = Image.open(path).convert("RGB")
    fine = np.asarray(im.resize((W, H), Image.BILINEAR)).astype(np.float32)
    coarse = np.asarray(im.resize((CW, CH), Image.BOX)).astype(np.float32)
    return gray(fine), gray(coarse)


def ssim(a, b, win=8):
    """Windowed SSIM: mean over non-overlapping win x win blocks. A single
    global SSIM is blind to layout (two unrelated frames have the same overall
    mean and variance), so blocks are what make it a layout metric."""
    h, w = a.shape
    hh, ww = h // win * win, w // win * win
    shape = (hh // win, win, ww // win, win)

    def blocks(x):
        return x[:hh, :ww].reshape(shape).transpose(0, 2, 1, 3).reshape(-1, win * win)

    aa, bb = blocks(a), blocks(b)
    ma = aa.mean(1, keepdims=True)
    mb = bb.mean(1, keepdims=True)
    va = aa.var(1)
    vb = bb.var(1)
    cov = ((aa - ma) * (bb - mb)).mean(1)
    c1 = (0.01 * 255.0) ** 2
    c2 = (0.03 * 255.0) ** 2
    s = ((2 * ma.ravel() * mb.ravel() + c1) * (2 * cov + c2)) / (
        (ma.ravel() ** 2 + mb.ravel() ** 2 + c1) * (va + vb + c2) + 1e-12
    )
    return float(s.mean())


def sobel(g):
    gx = np.zeros_like(g)
    gy = np.zeros_like(g)
    gx[:, 1:-1] = g[:, 2:] - g[:, :-2]
    gy[1:-1, :] = g[2:, :] - g[:-2, :]
    return np.sqrt(gx * gx + gy * gy) / 255.0


def corr_dist(a, b):
    a = a.ravel() - a.mean()
    b = b.ravel() - b.mean()
    return 1.0 - float((a * b).sum() / (np.sqrt((a * a).sum() * (b * b).sum()) + 1e-9))


def pairwise(items):
    """items: list of (fine, coarse). Returns (corr, ssim, edge, pairs)."""
    co, ss, el, pairs = [], [], [], 0
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            co.append(corr_dist(items[i][1], items[j][1]))
            ss.append(1.0 - ssim(items[i][0], items[j][0]))
            el.append(float(np.mean(np.abs(sobel(items[i][0]) - sobel(items[j][0])))))
            pairs += 1
    return co, ss, el, pairs


def render(binary, presets, seeds, out, seconds, cwd, every):
    for preset in presets:
        for seed in seeds:
            d = pathlib.Path(out) / preset / str(seed)
            d.mkdir(parents=True, exist_ok=True)
            have = d / "frame.png"
            if have.exists():
                continue
            tmp = tempfile.mkdtemp(prefix="magdiv-")
            env = dict(os.environ)
            env.update(
                {
                    "NCZ_FRAME_DUMP": tmp,
                    "NCZ_FRAME_DUMP_EVERY": str(every),
                    "NCZ_MAGMASIMPLEX_PRESET": preset,
                    "NCZ_MAGMASIMPLEX_SEED": str(seed),
                    "NCZ_MAGMASIMPLEX_PATTERN_SEED": "auto",
                }
            )
            try:
                subprocess.run(
                    ["timeout", "--kill-after=2", str(seconds), binary],
                    cwd=cwd, env=env, capture_output=True, timeout=seconds + 10,
                    check=False,
                )
            except subprocess.TimeoutExpired:
                pass
            frames = list(pathlib.Path(tmp).glob("*.png"))
            if not frames:
                print(f"FAIL {preset} seed {seed}: no frame dumped", file=sys.stderr)
            else:
                def idx(f):
                    try:
                        return int(f.stem.split("_")[1], 16)
                    except (IndexError, ValueError):
                        return -1
                warmed = [f for f in frames if idx(f) >= WARM_FRAME]
                pick = min(warmed, key=idx) if warmed else max(frames, key=idx)
                shutil.copy(pick, have)
            shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", help="output dir: <root>/<preset>/<seed>/frame.png")
    ap.add_argument("--render", metavar="BIN", help="render with this binary first")
    ap.add_argument("--preset", action="append", default=[], help="preset for --render (repeatable)")
    ap.add_argument("--seeds", nargs="*", type=int, default=[])
    ap.add_argument("--seconds", type=int, default=4)
    ap.add_argument("--every", type=int, default=60)
    ap.add_argument("--cwd", default=".")
    ap.add_argument("--threshold", type=float, default=0.25, help="min mean corr_dist")
    ap.add_argument("--pair-threshold", type=float, default=0.02, help="min corr_dist per pair")
    args = ap.parse_args()

    if args.render:
        if not args.preset or not args.seeds:
            ap.error("--render needs --preset and --seeds")
        render(args.render, args.preset, args.seeds, args.root, args.seconds, args.cwd, args.every)

    root = pathlib.Path(args.root)
    presets = sorted(p for p in root.iterdir() if p.is_dir()) if root.is_dir() else []
    if not presets:
        print(f"FAIL no presets under {root}")
        return 1
    fails = 0
    for p in presets:
        items = []
        for sd in sorted(p.iterdir()):
            if not sd.is_dir():
                continue
            f = sd / "frame.png"
            if not f.exists():
                f = next(iter(sorted(sd.glob("*.png"))), None)
            if f:
                items.append(load(f))
        if len(items) < 2:
            print(f"FAIL {p.name}: {len(items)} frames (need >= 2)")
            fails += 1
            continue
        co, ss, el, pairs = pairwise(items)
        mean_c, min_c = float(np.mean(co)), float(np.min(co))
        mean_s = float(np.mean(ss))
        mean_e = float(np.mean(el))
        ok = mean_c >= args.threshold and min_c >= args.pair_threshold
        fails += not ok
        print(
            "{} {:<16} seeds={:<2} pairs={:<3} corr_dist mean={:.4f} min={:.4f} "
            "(thr {:.2f}/{:.2f}) ssim_dist mean={:.4f} edge_l1 mean={:.4f}".format(
                "PASS" if ok else "FAIL", p.name, len(items), pairs,
                mean_c, min_c, args.threshold, args.pair_threshold, mean_s, mean_e,
            )
        )
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
