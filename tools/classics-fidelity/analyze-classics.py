#!/usr/bin/env python3
"""analyze-classics.py — Pillow + numpy fidelity/blur metrics for the classics
audit.  Replaces the magick-based pipeline so it runs anywhere (build-host, build-host,
NFS share) without external dependencies beyond Pillow and numpy.

Inputs:
    sys.argv[1] -- root directory containing captures/<hack>/<variant>/seed-N/
                   Each capture is expected to contain:
                     - one or more frame_NNNNNNNN.png files (full or scaled)
                     - summary.txt (with --render-size WxH and exit code)
                     - log.txt (harness diagnostic, optional)
    sys.argv[2] -- output TSV path

Metrics per capture (representative frame = frame_000000b4.png, falling back
to the median frame):

    nonblack        fraction of pixels whose R+G+B > 8 / 765
    mean_luma       mean luminance [0..1] (Rec. 709)
    luma_std        luminance standard deviation
    lit             fraction of pixels with luma > 0.10
    edge_density    fraction of pixels whose Sobel magnitude > 0.10
                    (Sobel is computed on a fixed-threshold autolevel,
                    threshold chosen so that the binary mask is meaningful
                    even on low-contrast frames).
    edge_sharpness  mean Sobel magnitude AT edge pixels (NOT autolevelled —
                    absolute magnitude; a blurred edge has lower Sobel
                    because intensity differences across the edge are smaller).
    blur_score      1 - min(1, edge_sharpness)
    hue_entropy     entropy of a 16-bin luma histogram
    color_shift     RMSE in [0..1] vs the classic seed-1 representative frame
    fingerprint     md5[0:8] of cropped-center 256x144 RGBA

All metrics are pure numpy; no ImageMagick call, no shell-out.
"""
import hashlib
import os
import re
import sys
import math
import glob
import numpy as np
from PIL import Image


def representative_frame(capture_dir):
    """Pick frame_000000b4.png or the median frame if missing."""
    rep = os.path.join(capture_dir, 'frame_000000b4.png')
    if os.path.exists(rep):
        return rep
    frames = sorted(glob.glob(os.path.join(capture_dir, 'frame_*.png')))
    if not frames:
        return None
    return frames[len(frames) // 2]


def count_frames(capture_dir):
    return sum(1 for _ in glob.glob(os.path.join(capture_dir, 'frame_*.png')))


def parse_summary(summary_path):
    info = {'surface': '', 'rc': 'NA'}
    if not os.path.exists(summary_path):
        return info
    text = open(summary_path).read()
    m = re.search(r'---- detected surface ----\s+(\d+x\d+)', text)
    if m:
        info['surface'] = m.group(1)
    else:
        m = re.search(r'render size (\d+x\d+)', text)
        if m:
            info['surface'] = m.group(1)
    m = re.search(r'---- exit ----\s+(\S+)', text)
    if m:
        info['rc'] = m.group(1)
    return info


def load_rgb(path):
    """Load as RGB uint8; returns (H, W, 3) numpy array."""
    img = Image.open(path).convert('RGB')
    return np.asarray(img, dtype=np.uint8)


def sobel_magnitude(gray_f):
    """Sobel magnitude per-pixel from a float32 grayscale image."""
    # 3x3 Sobel kernels (manual, separable via padding for speed)
    kx = np.array([[-1, 0, 1],
                   [-2, 0, 2],
                   [-1, 0, 1]], dtype=np.float32)
    ky = kx.T
    H, W = gray_f.shape
    # Pad with reflection
    p = np.pad(gray_f, 1, mode='reflect')
    gx = (
        kx[0, 0] * p[:-2, :-2] + kx[0, 1] * p[:-2, 1:-1] + kx[0, 2] * p[:-2, 2:] +
        kx[1, 0] * p[1:-1, :-2] + kx[1, 1] * p[1:-1, 1:-1] + kx[1, 2] * p[1:-1, 2:] +
        kx[2, 0] * p[2:, :-2] + kx[2, 1] * p[2:, 1:-1] + kx[2, 2] * p[2:, 2:]
    )
    gy = (
        ky[0, 0] * p[:-2, :-2] + ky[0, 1] * p[:-2, 1:-1] + ky[0, 2] * p[:-2, 2:] +
        ky[1, 0] * p[1:-1, :-2] + ky[1, 1] * p[1:-1, 1:-1] + ky[1, 2] * p[1:-1, 2:] +
        ky[2, 0] * p[2:, :-2] + ky[2, 1] * p[2:, 1:-1] + ky[2, 2] * p[2:, 2:]
    )
    return np.sqrt(gx * gx + gy * gy)


def fingerprint_md5(path):
    """md5[0:8] of cropped-center 256x144 RGBA."""
    img = Image.open(path).convert('RGBA')
    W, H = img.size
    cx, cy = W // 2, H // 2
    x0 = max(0, cx - 128); y0 = max(0, cy - 72)
    crop = img.crop((x0, y0, x0 + 256, y0 + 144))
    return hashlib.md5(crop.tobytes()).hexdigest()[:8]


def frame_metrics(path):
    rgb = load_rgb(path)
    r = rgb[..., 0].astype(np.float32) / 255.0
    g = rgb[..., 1].astype(np.float32) / 255.0
    b = rgb[..., 2].astype(np.float32) / 255.0
    luma = 0.2126 * r + 0.7152 * g + 0.0722 * b  # Rec. 709
    sum_rgb = r + g + b
    nonblack = float((sum_rgb > 0.032).mean())
    mean_l = float(luma.mean())
    std_l = float(luma.std())
    lit = float((luma > 0.10).mean())

    # Sobel on grayscale — compute both autolevelled (for the mask) and absolute
    gray_u8 = (luma * 255.0).round().astype(np.uint8)
    sob = sobel_magnitude(luma)  # absolute magnitude, NOT autolevelled
    # Normalize to [0..1] by dividing by max possible Sobel value for uint8 = 4*255 = 1020
    sob_norm = sob / 1020.0
    # Auto-levelled Sobel for edge detection (normalizes contrast so the mask is
    # meaningful even on low-contrast frames like cubestorm on black).
    if sob_norm.max() > sob_norm.min() + 1e-6:
        sob_auto = (sob_norm - sob_norm.min()) / (sob_norm.max() - sob_norm.min())
    else:
        sob_auto = sob_norm.copy()
    EDGE_THRESH = 0.10  # autolevelled sobel > 0.10 = edge pixel
    mask = sob_auto > EDGE_THRESH
    ed = float(mask.mean())
    if ed > 0:
        es = float(sob_norm[mask].mean())
    else:
        es = 0.0
    blur = max(0.0, 1.0 - min(1.0, es / 0.10))  # normalised to a 0..0.1 scale

    # Histogram entropy on luma, 16 bins
    hist, _ = np.histogram(luma, bins=16, range=(0.0, 1.0))
    total = hist.sum()
    ent = 0.0
    if total > 0:
        for c in hist:
            if c == 0:
                continue
            p = c / total
            ent -= p * math.log2(p)

    fp = fingerprint_md5(path)
    return dict(
        nonblack=nonblack, mean_luma=mean_l, luma_std=std_l, lit=lit,
        edge_density=ed, edge_sharpness=es, blur=blur,
        hue_entropy=ent, fingerprint=fp,
        sob_max_abs=float(sob_norm.max()), sob_mean_abs=float(sob_norm.mean()),
    )


def diff_rmse(a, b):
    if not os.path.exists(a) or not os.path.exists(b):
        return None
    A = np.asarray(Image.open(a).convert('RGB'), dtype=np.float32) / 255.0
    B = np.asarray(Image.open(b).convert('RGB'), dtype=np.float32) / 255.0
    if A.shape != B.shape:
        # Resize B to A
        h, w = A.shape[:2]
        b_img = Image.open(b).convert('RGB').resize((w, h), Image.LANCZOS)
        B = np.asarray(b_img, dtype=np.float32) / 255.0
    return float(np.sqrt(((A - B) ** 2).mean()))


def main():
    if len(sys.argv) < 3:
        print("usage: analyze-classics.py <root> <out_tsv>", file=sys.stderr)
        sys.exit(2)
    root = sys.argv[1]
    out_tsv = sys.argv[2]

    rows = []
    captures_root = os.path.join(root, 'captures')
    if not os.path.isdir(captures_root):
        print(f"no captures dir at {captures_root}", file=sys.stderr)
        sys.exit(3)

    # Stable ordering
    hacks = sorted(os.listdir(captures_root))
    for hack in hacks:
        hd = os.path.join(captures_root, hack)
        if not os.path.isdir(hd):
            continue
        for variant in sorted(os.listdir(hd)):
            vd = os.path.join(hd, variant)
            if not os.path.isdir(vd):
                continue
            for sd in sorted(os.listdir(vd)):
                sd_path = os.path.join(vd, sd)
                if not os.path.isdir(sd_path):
                    continue
                seed = sd.replace('seed-', '')
                summary = parse_summary(os.path.join(sd_path, 'summary.txt'))
                nframes = count_frames(sd_path)
                rep = representative_frame(sd_path)
                if rep is None:
                    continue
                m = frame_metrics(rep)
                if m is None:
                    continue
                # Color shift vs classic seed-1 (only meaningful at same seed)
                cshift = None
                if variant != 'classic' and seed == '1':
                    ref = os.path.join(hd, 'classic', 'seed-1',
                                       'frame_000000b4.png')
                    cshift = diff_rmse(ref, rep)
                rows.append(dict(
                    hack=hack, variant=variant, seed=seed,
                    surface=summary['surface'], rc=summary['rc'],
                    nframes=nframes,
                    nonblack=m['nonblack'], mean_luma=m['mean_luma'],
                    luma_std=m['luma_std'], lit=m['lit'],
                    edge_density=m['edge_density'],
                    edge_sharpness=m['edge_sharpness'],
                    blur=m['blur'],
                    hue_entropy=m['hue_entropy'],
                    fingerprint=m['fingerprint'],
                    color_shift=cshift,
                    sob_max_abs=m['sob_max_abs'],
                    sob_mean_abs=m['sob_mean_abs'],
                ))

    cols = ['hack', 'variant', 'seed', 'surface', 'rc', 'nframes',
            'nonblack', 'mean_luma', 'luma_std', 'lit',
            'edge_density', 'edge_sharpness', 'blur',
            'hue_entropy', 'color_shift', 'fingerprint',
            'sob_max_abs', 'sob_mean_abs']
    with open(out_tsv, 'w') as fh:
        fh.write('\t'.join(cols) + '\n')
        for r in rows:
            vals = []
            for c in cols:
                v = r[c]
                if v is None:
                    vals.append('NA')
                elif isinstance(v, float):
                    vals.append(f'{v:.6f}')
                else:
                    vals.append(str(v))
            fh.write('\t'.join(vals) + '\n')

    print(f"wrote {len(rows)} rows to {out_tsv}", file=sys.stderr)


if __name__ == '__main__':
    main()
