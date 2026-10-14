#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""magmasimplex-realism-gates.py - measurable "does this look like lamp wax"
gates on captured frames, with thresholds taken from real-lamp footage.

The operator's 2026-10-04 review of the randomized run on O6N: the orange
look (IMG_8237) was "not wax": glossy candy highlights, ring-outlined
bubbles, an even white speckle, identical glossy ellipses. The reference
frames (docs/MAGMASIMPLEX-REALISM.md) have none of that. Each gate below
measures one of those defects on a frame; the thresholds come from the
reference frames (`calibrate`) and were checked to FAIL on the orange look
and PASS on the fixed output (docs/MAGMASIMPLEX-REALISM.md, "Gates").

Frames are analysed at 540 px high (the crop's height, so a reference globe
and a screenshot are compared at one lamp-relative scale). The wax is separated from the liquid by a
two-cluster split in colour; the cluster that owns the side borders at
mid-height is the liquid (the globe's sides are liquid in every reference
frame and in the saver's lamp views). `--swap` overrides that choice.

metrics (per frame, printed as JSON with `measure`):
  speckle_per_mpx  small bright isolated specks in the liquid (white top-hat
                   7x7 > 20/255, area <= 10 px), per megapixel at 960 wide
  thin_bright      share of liquid pixels that are thin bright structures
                   (top-hat 9x9 > 20/255): speckle, glitter, ring outlines
  spec_per_100k    glossy point highlights on the wax: compact bright spots
                   (top-hat 21x21 > 45/255, 4..600 px) inside the wax, per
                   100k wax pixels
  edge_rel         softness of the wax silhouette: the width of the
                   luminance transition at the boundary, divided by the
                   median free-blob diameter (0.01 = razor outline)
  ellipse_iou      median IoU of each free blob with its moment-fitted
                   ellipse (1.0 = a perfect ellipse; real wax is lumpy)
  aspect_cv        coefficient of variation of the free blobs' aspect ratios
                   (0 = repeating identical shapes)
  wax_over_liquid  mean luminance of the wax over the liquid's, lower 60 % of
                   the frame (real wax glows brighter than the liquid)
  wax_lum_cv       luminance variation inside the wax (translucency gradient)
  n_blobs          free blobs found (not touching top or bottom)

usage:
  magmasimplex-realism-gates.py measure IMG [--crop x0,y0,x1,y1] [--swap]
  magmasimplex-realism-gates.py gate IMG... [--crop ...] [--thresholds F.json]
  magmasimplex-realism-gates.py calibrate LIST.tsv      (path<TAB>crop rows)
`gate` prints PASS/FAIL per gate and exits 1 on any failure. Crops are
fractions of width/height (0..1) or pixels of the source image.
"""

import json
import math
import pathlib
import sys

import numpy as np
from PIL import Image

try:
    from scipy import ndimage as ndi
except ImportError:  # pragma: no cover - the fleet hosts have scipy
    ndi = None

HEIGHT = 540
SPECK_AMP = 24.0  # top-hat amplitude (of 255) above video-compression noise

# Gates, with thresholds from the reference frames (calibrate over 37 globe
# crops of the three videos, 2026-10-05; table and the exact values in
# docs/MAGMASIMPLEX-REALISM.md). Only the four metrics that separate the
# references from the rejected look are gates; edge_rel, ellipse_iou and
# aspect_cv are reported for the eye but not gated, because real lamps span
# their whole range (a blob in clear liquid films with a crisp edge, a lone
# rising blob is a near-perfect ellipse).
DEFAULT_THRESHOLDS = {
    "speckle_per_mpx": {"max": 40.0},
    "thin_bright": {"max": 0.003},
    "spec_per_100k": {"max": 12.0},
    "spec_blob_share": {"max": 0.4},
    "wax_over_liquid": {"min": 0.6},
}


def _need_scipy():
    if ndi is None:
        raise SystemExit("magmasimplex-realism-gates: python3-scipy is required")


def load(path, crop=None, height=HEIGHT):
    im = Image.open(path).convert("RGB")
    if crop:
        x0, y0, x1, y1 = crop
        if max(crop) <= 1.0:
            x0, x1 = x0 * im.width, x1 * im.width
            y0, y1 = y0 * im.height, y1 * im.height
        im = im.crop((int(x0), int(y0), int(x1), int(y1)))
    # One lamp-relative scale for every frame: the crop (a globe, or the
    # saver's screen) is resampled to HEIGHT px high, so a "speck" or a
    # "highlight" means the same size in a 360p reference globe and a 1080p
    # screenshot. Densities are per megapixel at this scale.
    w = max(8, round(im.width * height / im.height))
    im = im.resize((w, height), Image.LANCZOS)
    return np.asarray(im).astype(np.float32)


def luminance(rgb):
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def segment(rgb, swap=False, iters=12):
    """Two-cluster split in colour. Returns (wax_mask, liquid_mask)."""
    h, w, _ = rgb.shape
    lum = luminance(rgb)
    s = rgb.sum(axis=2) + 1e-3
    chroma = rgb / s[..., None]  # rg chromaticity (3 channels summing to 1)
    feat = np.concatenate(
        [chroma * 3.0, (lum / 255.0)[..., None] * 1.2], axis=2
    ).reshape(-1, 4)
    # k-means, k=2, seeded on the two most different pixels of a sample
    rng = np.random.default_rng(1)
    sample = feat[
        rng.choice(feat.shape[0], size=min(20000, feat.shape[0]), replace=False)
    ]
    c0 = sample[np.argmin(sample[:, 3])]
    d = ((sample - c0) ** 2).sum(axis=1)
    c1 = sample[np.argmax(d)]
    cent = np.stack([c0, c1])
    for _ in range(iters):
        dist = ((sample[:, None, :] - cent[None, :, :]) ** 2).sum(axis=2)
        lab = dist.argmin(axis=1)
        for k in range(2):
            if (lab == k).any():
                cent[k] = sample[lab == k].mean(axis=0)
    dist = ((feat[:, None, :] - cent[None, :, :]) ** 2).sum(axis=2)
    labels = dist.argmin(axis=1).reshape(h, w)
    # The liquid owns the side borders at mid-height.
    band = labels[h // 4 : 3 * h // 4]
    side = np.concatenate([band[:, :4].ravel(), band[:, -4:].ravel()])
    liquid_label = 1 if (side == 1).mean() > 0.5 else 0
    if swap:
        liquid_label = 1 - liquid_label
    wax = labels != liquid_label
    # Clean: drop specks, then fill every hole (a glossy highlight clusters
    # with a pale liquid and would otherwise punch a hole in its own blob),
    # so motes and glitter never count as wax and highlights stay inside it.
    wax = ndi.binary_opening(wax, iterations=2)
    wax = ndi.binary_closing(wax, iterations=2)
    wax = ndi.binary_fill_holes(wax)
    return wax, ~wax


def tophat(lum, size):
    return lum - ndi.grey_opening(lum, size=(size, size))


def components(mask):
    lab, n = ndi.label(mask)
    if n == 0:
        return lab, n, np.zeros(0, int)
    areas = ndi.sum(mask, lab, index=np.arange(1, n + 1)).astype(int)
    return lab, n, areas


def ellipse_iou(comp):
    """IoU of a boolean component with the ellipse of the same moments."""
    ys, xs = np.nonzero(comp)
    area = xs.size
    if area < 20:
        return float("nan"), float("nan")
    cx, cy = xs.mean(), ys.mean()
    dx, dy = xs - cx, ys - cy
    mxx, myy, mxy = (dx * dx).mean(), (dy * dy).mean(), (dx * dy).mean()
    cov = np.array([[mxx, mxy], [mxy, myy]])
    evals, evecs = np.linalg.eigh(cov)
    evals = np.maximum(evals, 1e-6)
    a, b = 2.0 * np.sqrt(evals[1]), 2.0 * np.sqrt(evals[0])  # semi-axes
    # Scale so the ellipse has the component's area.
    scale = np.sqrt(area / (np.pi * a * b))
    a, b = a * scale, b * scale
    yy, xx = np.mgrid[0 : comp.shape[0], 0 : comp.shape[1]]
    px, py = xx - cx, yy - cy
    u = px * evecs[0, 1] + py * evecs[1, 1]
    v = px * evecs[0, 0] + py * evecs[1, 0]
    ell = (u / a) ** 2 + (v / b) ** 2 <= 1.0
    inter = (ell & comp).sum()
    union = (ell | comp).sum()
    return float(inter / max(union, 1)), float(a / max(b, 1e-6))


def measure(rgb, swap=False):
    _need_scipy()
    h, w, _ = rgb.shape
    lum = luminance(rgb)
    wax, liquid = segment(rgb, swap)
    out = {"width": w, "height": h, "wax_share": float(wax.mean())}

    # Speckle and thin bright structures in the liquid.
    th7 = tophat(lum, 7)
    cand = (th7 > SPECK_AMP) & liquid
    lab, n, areas = components(cand)
    small = (areas <= 10).sum() if n else 0
    out["speckle_per_mpx"] = small / (w * h / 1e6)
    th9 = tophat(lum, 9)
    out["thin_bright"] = float(
        ((th9 > SPECK_AMP) & liquid).sum() / max(liquid.sum(), 1)
    )

    # Glossy highlights on the wax: per blob, compact spots much brighter
    # than the blob's own median (40/255), well inside it, and small against
    # it (at most 6 % of its area). A real lamp's blown-out core is bright
    # too, but it is a large part of its blob; a translucency gradient is
    # large as well. Lumps at the boundary are excluded by the erosion.
    # A glossy highlight is also WHITER than its blob (a reflection of the
    # light, not the dye): its saturation is under half the blob's median.
    mx = rgb.max(axis=2)
    mn = rgb.min(axis=2)
    sat = np.where(mx > 1.0, (mx - mn) / np.maximum(mx, 1.0), 0.0)
    wlab, wn, wareas = components(wax)
    inner = ndi.binary_erosion(wax, iterations=5)
    nspec, nblob, nblob_spec = 0, 0, 0
    for b in range(1, wn + 1):
        barea = int(wareas[b - 1])
        if barea < 200:
            continue
        nblob += 1
        blob = wlab == b
        med = float(np.median(lum[blob]))
        msat = float(np.median(sat[blob]))
        hot = blob & inner & (lum > med + 40.0) & (sat < 0.5 * msat)
        if not hot.any():
            continue
        hl, _hn, hareas = components(hot)
        found = 0
        for i, a in enumerate(hareas, start=1):
            if a < 4 or a > 0.06 * barea:
                continue
            ys, xs = np.nonzero(hl == i)
            bbox = (np.ptp(ys) + 1) * (np.ptp(xs) + 1)
            if a / bbox >= 0.45:  # compact, not a sliver
                found += 1
        nspec += found
        nblob_spec += found > 0
    out["spec_per_100k"] = nspec / max(wax.sum() / 1e5, 1e-6)
    # A glazed material carries a highlight on EVERY body; a glass glint
    # over a real lamp sits on one or two.
    # (spec_per_100k alone cannot tell gloss from bulb light transmitted
    # through a thin body at the hot centre, which is also bright and
    # desaturated: its gate is the references' maximum; the share is the
    # discriminating gate.)
    # ...with at least four bodies, else the share is noise.
    out["spec_blob_share"] = float(nblob_spec / nblob) if nblob >= 4 else float("nan")

    # Silhouette softness: luminance transition width at the wax boundary.
    gy, gx = np.gradient(ndi.gaussian_filter(lum, 0.7))
    grad = np.hypot(gx, gy)
    er = ndi.binary_erosion(wax, iterations=1)
    boundary = wax & ~er
    near_w = wax & ~ndi.binary_erosion(wax, iterations=6)
    near_l = liquid & ~ndi.binary_erosion(liquid, iterations=6)
    if boundary.any() and near_w.any() and near_l.any():
        contrast = abs(float(np.median(lum[near_w])) - float(np.median(lum[near_l])))
        g = float(np.median(grad[boundary]))
        width_px = contrast / max(g, 1e-3)
    else:
        contrast, width_px = 0.0, float("nan")
    out["edge_contrast"] = contrast
    out["edge_width_px"] = width_px

    # Free blobs: wax components not touching the top or bottom rows.
    lab, n, areas = components(wax)
    ious, aspects, diams = [], [], []
    for i, a in enumerate(areas, start=1):
        if a < 0.002 * w * h:
            continue
        comp = lab == i
        if comp[0, :].any() or comp[-1, :].any():
            continue
        iou, asp = ellipse_iou(comp)
        if np.isnan(iou):
            continue
        ious.append(iou)
        aspects.append(asp)
        diams.append(2.0 * np.sqrt(a / np.pi))
    out["n_blobs"] = len(ious)
    out["ellipse_iou"] = float(np.median(ious)) if ious else float("nan")
    out["aspect_cv"] = (
        float(np.std(aspects) / max(np.mean(aspects), 1e-6))
        if len(aspects) >= 2
        else float("nan")
    )
    med_d = float(np.median(diams)) if diams else float("nan")
    out["blob_diam_px"] = med_d
    out["edge_rel"] = (
        float(width_px / med_d) if diams and not math.isnan(width_px) else float("nan")
    )

    # Luminance order and internal variation.
    lower = np.zeros((h, w), bool)
    lower[int(h * 0.4) :, :] = True
    wl, ll = wax & lower, liquid & lower
    if wl.any() and ll.any():
        out["wax_over_liquid"] = float(lum[wl].mean() / max(lum[ll].mean(), 1.0))
    else:
        out["wax_over_liquid"] = float("nan")
    core = ndi.binary_erosion(wax, iterations=4)
    out["wax_lum_cv"] = (
        float(lum[core].std() / max(lum[core].mean(), 1.0))
        if core.any()
        else float("nan")
    )
    return out


def parse_crop(s):
    if not s or s in ("-", "none"):
        return None
    v = [float(x) for x in s.split(",")]
    if len(v) != 4:
        raise SystemExit("crop must be x0,y0,x1,y1")
    return v


def gate(metrics, thresholds=None):
    """Return a list of (name, ok, value, limit_text); NaN values are skipped
    (a frame without free blobs has no shape gate)."""
    th = thresholds or DEFAULT_THRESHOLDS
    rows = []
    for name, lim in th.items():
        v = metrics.get(name)
        if v is None or math.isnan(v):
            rows.append((name, True, v, "n/a"))
            continue
        ok = True
        txt = []
        if "max" in lim:
            ok &= v <= lim["max"]
            txt.append(f"<= {lim['max']:g}")
        if "min" in lim:
            ok &= v >= lim["min"]
            txt.append(f">= {lim['min']:g}")
        rows.append((name, bool(ok), v, " ".join(txt)))
    return rows


def main(argv):
    if len(argv) < 2 or argv[1] not in ("measure", "gate", "calibrate"):
        print(__doc__)
        return 2
    cmd = argv[1]
    args = argv[2:]
    crop, swap, thr = None, False, None
    files = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--crop":
            crop = parse_crop(args[i + 1])
            i += 2
        elif a == "--swap":
            swap = True
            i += 1
        elif a == "--thresholds":
            thr = json.loads(pathlib.Path(args[i + 1]).read_text())
            i += 2
        else:
            files.append(a)
            i += 1
    if cmd == "calibrate":
        rows = []
        for line in pathlib.Path(files[0]).read_text().splitlines():
            if not line.strip() or line.startswith("#"):
                continue
            parts = line.rstrip("\n").split("\t")
            path = parts[0]
            c = parse_crop(parts[1]) if len(parts) > 1 else None
            sw = len(parts) > 2 and parts[2].strip() == "swap"
            m = measure(load(path, c), sw)
            m["file"] = path
            rows.append(m)
            print(json.dumps(m, sort_keys=True))
        keys = [k for k in DEFAULT_THRESHOLDS] + [
            "edge_rel",
            "ellipse_iou",
            "aspect_cv",
            "wax_lum_cv",
            "edge_width_px",
            "n_blobs",
        ]
        print(f"\n# percentile table over {len(rows)} frames (p0 / p50 / p100)")
        for k in keys:
            v = np.array([r[k] for r in rows if r.get(k) == r.get(k)], float)
            if v.size:
                print(
                    f"{k:<18} {v.min():8.4f} {np.median(v):8.4f} {v.max():8.4f}  (n={v.size})"
                )
        return 0
    fails = 0
    for f in files:
        m = measure(load(f, crop), swap)
        if cmd == "measure":
            m["file"] = f
            print(json.dumps(m, sort_keys=True, indent=1))
            continue
        print(f)
        for name, ok, v, lim in gate(m, thr):
            fails += not ok
            vs = "n/a" if v is None or math.isnan(v) else f"{v:.4f}"
            print(f"  {'PASS' if ok else 'FAIL'} {name:<16} {vs} ({lim})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
