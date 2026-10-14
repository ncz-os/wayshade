#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measurements for the chitin saver (docs/CHITIN.md): scale hierarchy, bilateral asymmetry,
palette discipline, warp octave count.

usage: chitin-measure.py --render build/xshadertoy_render [--root .] [--out DIR] [--size 1920x1080]

  scale     renders with warp=0 and reads the luminance spectrum in cycles per screen height:
            ribs must peak at 3, conduits at 12, pores at 60 (or its staggered 120).
            Also prints the same peaks with the default warp (they broaden, not move).
  asym      renders the nave with the light straight above and not drifting, finds the mirror
            axis, and reports A = sum|L - L_mirror| / sum(L + L_mirror) for asymmetry 0.05, 0.10,
            0.15 over several seeds. Target: A within 0.05..0.15 at the option's own range.
  palette   over every scene and several seeds: share of pixels whose Oklab chroma exceeds 0.015 and whose hue differs from the body hue by more than 45 degrees (patina and accent runs)
            (the accent; must be <= 8 percent), any pixel with chroma > 0.09 in the red hues
            (none allowed), any bright bluish pixel (none allowed), share of near-monochrome pixels.
  octaves   counts the noise evaluations in the shader's warp (3 octaves x 2 channels).

Exit status 1 when any gate fails.
"""

import argparse
import pathlib
import re
import subprocess
import sys

import numpy as np
from PIL import Image


def render(binary, shader, out, tag, t, size, seed, opts):
    cmd = [binary, str(shader), "--out", str(out), "--tag", tag, "--times", f"{t:g}",
           "--size", size, f"--seed={seed}"] + [f"--{o}" for o in opts]
    p = subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=300)
    if p.returncode != 0:
        sys.exit(f"render failed: {' '.join(cmd)}\n{p.stderr[-1500:]}")
    return np.asarray(Image.open(out / f"{tag}_t{t:07.2f}.png").convert("RGB"), dtype=np.float64) / 255.0


def lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def luma(img):
    l = lin(img)
    return 0.2126 * l[..., 0] + 0.7152 * l[..., 1] + 0.0722 * l[..., 2]


def oklab(img):
    c = lin(img)
    l = np.cbrt(0.4122214708 * c[..., 0] + 0.5363325363 * c[..., 1] + 0.0514459929 * c[..., 2])
    m = np.cbrt(0.2119034982 * c[..., 0] + 0.6806995451 * c[..., 1] + 0.1073969566 * c[..., 2])
    s = np.cbrt(0.0883024619 * c[..., 0] + 0.2817188376 * c[..., 1] + 0.6299787005 * c[..., 2])
    return (0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s)


def peaks(sig_len_cycles, spec, lo, hi, n=1, raw=False):
    """(cycle, prominence) of the most prominent bin in [lo, hi] cycles per screen height:
    the bin's amplitude over the running median of its neighbourhood (so a bright, broad
    low-frequency structure does not hide a narrow line)."""
    f = np.arange(len(spec)) / sig_len_cycles
    k = max(3, int(round(6 * sig_len_cycles)))          # about +-6 cycles per height
    prom = np.empty_like(spec)
    for i in range(len(spec)):
        if raw:
            prom[i] = spec[i]
            continue
        prom[i] = spec[i] / (np.median(spec[max(0, i - k):i + k + 1]) + 1e-12)
    idx = np.where((f >= lo) & (f <= hi))[0]
    order = idx[np.argsort(prom[idx])[::-1][:n]]
    return [(float(f[i]), float(prom[i])) for i in order]


def spectra(L):
    """Mean amplitude spectra: vertical (down the columns) and horizontal (along the rows),
    frequencies in cycles per screen height. L is (H, W)."""
    H, W = L.shape
    win_v = np.hanning(H)[:, None]
    win_h = np.hanning(W)[None, :]
    a = L - L.mean()
    sv = np.abs(np.fft.rfft(a * win_v, axis=0)).mean(axis=1)
    sh = np.abs(np.fft.rfft(a * win_h, axis=1)).mean(axis=0)
    # x spans W/H screen heights, y spans 1 (H rows = 1 screen height of 2 uv units)
    return sv, 1.0, sh, W / H


def scale_gate(a, out, size, report):
    ok = True
    for form, rib_axis, cnd_axis, extra in [("nave", "v", "h", ["conduits=1", "pores=1"]),
                                            ("comb", "h", "v", ["conduits=1", "pores=1"])]:
        for warp in (0.0, 0.5):
            L = luma(render(a.render, a.shader, out, f"sc_{form}_{warp}", 3, size, 4242,
                            [f"form={form}", "randomize=false", "light-drift=0", "asymmetry=0.05",
                             f"warp={warp}", "evolution=0", "scroll=0"] + extra))
            Lc = L[:, L.shape[1] // 2 - 110:L.shape[1] // 2 + 110] if form == "nave" else L   # nave ribs arch: read the central strip
            sv, lv, sh, lh = spectra(Lc)
            if form == "nave":
                sh, lh = spectra(L)[2:]
            vert, horiz = (sv, lv), (sh, lh)
            rib = peaks(vert[1] if rib_axis == "v" else horiz[1],
                        (vert if rib_axis == "v" else horiz)[0], 1.5, 4.5, raw=True)[0]
            cnd = peaks(vert[1] if cnd_axis == "v" else horiz[1],
                        (vert if cnd_axis == "v" else horiz)[0], 9.5, 15)[0]
            pore = max(peaks(horiz[1], horiz[0], 54, 66)[0], peaks(horiz[1], horiz[0], 108, 132)[0], key=lambda r: r[1])
            good = abs(rib[0] - 3) <= 0.7 and abs(cnd[0] - 12) <= 1.6 and (abs(pore[0] - 60) <= 6 or abs(pore[0] - 120) <= 10)
            if warp == 0.0:
                ok &= good
            report.append(f"| scale | {form} warp={warp} | rib {rib[0]:.1f}/H (want 3), conduit {cnd[0]:.1f}/H (want 12), "
                          f"pore {pore[0]:.1f}/H (want 60 or 120) | {'PASS' if good else ('n/a (warped)' if warp else 'FAIL')} |")
    return ok


def mirrored_diff(L, c):
    """Mean |L(x) - L(2c - x)| and mean (L + L_mirror), c a float pixel-center column."""
    H, W = L.shape
    half = int(min(c, W - 1 - c)) - 6
    x = np.arange(1, half)                     # distance from the axis, in pixels
    left, right = c - x, c + x
    def samp(u):
        i0 = np.floor(u).astype(int)
        f = (u - i0)[None, :]
        return L[:, i0] * (1 - f) + L[:, i0 + 1] * f
    l, r = samp(left), samp(right)
    return np.abs(l - r).sum(), (l + r).sum()


def asym_metric(L):
    """A = sum|L - L_mirror| / sum(L + L_mirror) about the best mirror axis, searched to a
    twentieth of a pixel (a half-pixel axis error alone would add to every sharp edge)."""
    H, W = L.shape
    c0 = W // 2
    best = min(((mirrored_diff(L, float(c))[0], float(c)) for c in range(c0 - 60, c0 + 61)))[1]
    cand = [best + d for d in np.arange(-1.0, 1.0001, 0.05)]
    c = min(cand, key=lambda u: mirrored_diff(L, u)[0])
    d, n = mirrored_diff(L, c)
    return float(d / n)


def asym_gate(a, out, size, report):
    ok = True
    for asym in (0.05, 0.10, 0.15):
        vals = []
        for seed in (11, 4242, 90210, 777):
            L = luma(render(a.render, a.shader, out, f"as_{asym}_{seed}", 3, size, seed,
                            ["form=nave", "randomize=false", "light=0.5", "light-drift=0",
                             f"asymmetry={asym}", "evolution=0", "scroll=0"]))
            vals.append(asym_metric(L))
        m = float(np.mean(vals))
        good = 0.05 <= m <= 0.15
        ok &= good
        report.append(f"| asym | option {asym:.2f} | A = {m:.3f} (per seed {', '.join(f'{v:.3f}' for v in vals)}), want 0.05..0.15 | {'PASS' if good else 'FAIL'} |")
    return ok


def palette_gate(a, out, size, report):
    root = pathlib.Path(a.root)
    scenes = sorted(p.stem for p in (root / "assets/screensaver-chooser/options/presets/xshadertoy_chitin").glob("*.conf"))
    ok = True
    for sc in scenes:
        acc, strong, mono, red, blue = [], [], [], 0, 0
        for seed in (11, 4242, 90210):
            for t in (5, 60):
                img = render(a.render, a.shader, out, f"pl_{sc}_{seed}_{t}", t, size, seed, [f"preset={sc}"])
                L, A, B = oklab(img)
                C = np.hypot(A, B)
                hue = np.degrees(np.arctan2(B, A)) % 360
                w = C > 0.008
                # dominant (body) hue: circular mean of the tinted pixels; the accent is what departs from it
                dom = np.degrees(np.arctan2(np.sin(np.radians(hue[w])).mean(), np.cos(np.radians(hue[w])).mean())) % 360 if w.any() else 0.0
                dh = np.abs((hue - dom + 180) % 360 - 180)
                acc.append(float(((C > 0.015) & (dh > 45)).mean()))
                strong.append(float((C > 0.04).mean()))
                mono.append(float((C < 0.03).mean()))
                red += int(((C > 0.09) & ((hue < 40) | (hue > 340))).sum())
                blue += int(((L > 0.85) & (B < -0.03)).sum())
        good = max(acc) <= 0.08 and red == 0 and blue == 0
        ok &= good
        report.append(f"| palette | {sc} | accent (chroma>0.015 and >45 deg from the body hue) share max {max(acc)*100:.2f}% (mean {np.mean(acc)*100:.2f}%, want <= 8%), strongly colored (>0.04) {max(strong)*100:.2f}%, "
                      f"near-monochrome {np.mean(mono)*100:.1f}%, saturated-red px {red}, blue-white px {blue} | {'PASS' if good else 'FAIL'} |")
    return ok


def octave_gate(a, report):
    src = pathlib.Path(a.shader).read_text()
    body = src[src.index("void warp3"):src.index("void mainImage")]
    n = len(re.findall(r"xst_noised\(", body))
    good = n == 6
    report.append(f"| octaves | warp3 | {n} noise evaluations = {n // 2} octaves x 2 channels (want 3 octaves) | {'PASS' if good else 'FAIL'} |")
    other = [m for m in re.findall(r"xst_noise[d]?\(", src[:src.index("void warp3")] + src[src.index("void mainImage"):])]
    good2 = not other
    report.append(f"| octaves | elsewhere | noise calls outside the warp: {len(other)} (noise only warps; want 0) | {'PASS' if good2 else 'FAIL'} |")
    return good and good2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", required=True)
    ap.add_argument("--root", default=".")
    ap.add_argument("--out", default="chitin-measure-out")
    ap.add_argument("--size", default="1920x1080")
    a = ap.parse_args()
    a.shader = pathlib.Path(a.root) / "vendor/xshadertoy/glsl/chitin.glsl"
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    report = ["| gate | subject | measurement | verdict |", "|---|---|---|---|"]
    ok = octave_gate(a, report)
    ok &= scale_gate(a, out, a.size, report)
    ok &= asym_gate(a, out, a.size, report)
    ok &= palette_gate(a, out, a.size, report)
    text = "\n".join(report)
    (out / "measurements.md").write_text(text + "\n")
    print(text)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
