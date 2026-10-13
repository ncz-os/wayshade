#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""magmasimplex-frame-gates.py - image gates on frames the harness dumped
(NCZ_FRAME_DUMP / NCZ_FRAME_DUMP_EVERY), for docs/MAGMASIMPLEX-60S.md.

  flat   (every frame): the operator's complaint was a flat, single-colour
         background. FAIL when more than 25 % of 32x32 tiles are flat (luminance
         standard deviation under 1.5/255 inside the tile AND tile mean within
         3/255 of all its neighbours), or when the frame's p5-p95 luminance
         spread is under 25/255.
  smooth (mean over frames): the operator's actual complaint, a smooth low-
         contrast field. FAIL when more than 25 % of 32x32 tiles have a
         luminance standard deviation under 3/255. Measured: the previous goo
         engine 0.49; the sixties views 0.03-0.08.
  flash  (the sequence): no luminance oscillation faster than 3 Hz. For the
         whole frame and for each cell of a 16x9 grid, remove the slow trend
         (1 s moving average) and take the spectrum; FAIL when any component
         above 3 Hz exceeds 4/255 in amplitude (a flash is a 10 % swing or more;
         the margin is deliberate). Frames must be evenly spaced: pass the
         sampling rate (render fps / dump interval).

  dendrite (PNG frame dir): the branching-layer metric the brief asks for.
         edges = share of pixels whose colour gradient exceeds 0.05 (about
         13/255): thin high-contrast fingers raise it, a smooth lamp look does
         not. spread = 1 - |mean exp(2i*angle)| over edge pixels, so a network
         or fan in every orientation is near 1. score = edges. coverage =
         share of non-background pixels (luminance above 20/255), i.e. how much
         of the frame a look fills. Reported always on a PNG dir; gate with
         `--dendrite-min X` / `--dendrite-max X` / `--coverage-min X`, or run
         just this with `--dendrite-only`. A VIDEO (for example a wf-recorder
         capture of the compositor output) is decoded with ffmpeg at exactly
         SAMPLE_HZ frames per second; that is the reliable way to sample evenly
         (PNG dumps stall the renderer), but video frames carry no RGB metric.

usage: magmasimplex-frame-gates.py FRAMES_DIR|VIDEO SAMPLE_HZ [--flat-only] [--dendrite-only]
       [--dendrite-min X] [--dendrite-max X] [--coverage-min X]
Prints one line per gate and exits 1 on any failure.
"""

import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image


def lum(path, w=480):
    im = Image.open(path).convert("RGB")
    h = int(im.height * w / im.width)
    a = np.asarray(im.resize((w, h), Image.BILINEAR)).astype(np.float32)
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def video_frames(path, hz, w=480):
    probe = subprocess.run(
        [
            "ffprobe",
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=width,height",
            "-of",
            "csv=p=0",
            path,
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    sw, sh = (int(v) for v in probe.split(",")[:2])
    h = (sh * w // sw) // 2 * 2
    cmd = ["ffmpeg", "-loglevel", "error", "-i", path, "-vf", f"fps={hz},scale={w}:{h}", "-f", "rawvideo"]
    cmd += ["-pix_fmt", "rgb24", "-"]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    n = w * h * 3
    while True:
        b = p.stdout.read(n)
        if len(b) < n:
            break
        a = np.frombuffer(b, np.uint8).reshape(h, w, 3).astype(np.float32)
        yield 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    p.wait()


def rgb_frame(path, w=480):
    """The frame resized to `w` wide, RGB in 0..1."""
    im = Image.open(path).convert("RGB")
    h = int(im.height * w / im.width)
    return np.asarray(im.resize((w, h), Image.BILINEAR)).astype(np.float32) / 255.0


def dendrite_metrics(a):
    """(edge density, gradient-orientation spread, coverage) of an RGB frame.

    edge density: share of pixels whose colour gradient (max over channels)
    exceeds 0.05 (about 13/255). A branching layer draws many thin,
    high-contrast fingers; smooth lamp looks do not.
    spread: 1 - |mean exp(2i*angle)| over the edge pixels, 0 when every edge
    runs one way and 1 when all orientations are present (a network/fan).
    coverage: share of non-background pixels (luminance above 20/255), i.e.
    how much of the frame the look fills.
    """
    gx = np.zeros(a.shape[:2], np.float32)
    gy = np.zeros(a.shape[:2], np.float32)
    for c in range(3):
        gy_c, gx_c = np.gradient(a[..., c])
        gx = np.maximum(gx, np.abs(gx_c))
        gy = np.maximum(gy, np.abs(gy_c))
    lum = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    cov = float((lum > 20.0 / 255.0).mean())
    mag = np.hypot(gx, gy)
    e = mag > 0.05
    n = int(e.sum())
    if n < 100:
        return 0.0, 0.0, cov
    ang = np.arctan2(gy[e], gx[e])
    spread = 1.0 - float(abs(np.mean(np.exp(2j * ang))))
    return float(e.mean()), spread, cov


def flat_fraction(y):
    h, w = y.shape
    th, tw = h // 32 * 32, w // 32 * 32
    t = y[:th, :tw].reshape(th // 32, 32, tw // 32, 32)
    std = t.std(axis=(1, 3))
    smooth = float((std < 3.0).mean())
    mean = t.mean(axis=(1, 3))
    pad = np.pad(mean, 1, mode="edge")
    nb = np.max(np.abs(np.stack([pad[1:-1, :-2], pad[1:-1, 2:], pad[:-2, 1:-1], pad[2:, 1:-1]]) - mean), axis=0)
    flat = (std < 1.5) & (nb < 3.0)
    return float(flat.mean()), float(np.percentile(y, 95) - np.percentile(y, 5)), smooth


def main():
    d = pathlib.Path(sys.argv[1])
    hz = float(sys.argv[2])
    if d.is_file():
        lums = list(video_frames(str(d), hz))
    else:
        lums = [lum(f) for f in sorted(d.glob("*.png"))]
    if not lums:
        print("FAIL no frames in", d)
        return 1
    frames = lums
    fails = 0
    worst_flat, worst_spread, smooth_sum = 0.0, 1e9, 0.0
    series, cells = [], []
    for y in frames:
        ff, sp, sm = flat_fraction(y)
        smooth_sum += sm
        worst_flat, worst_spread = max(worst_flat, ff), min(worst_spread, sp)
        if "--flat-only" in sys.argv:
            continue
        series.append(y.mean())
        h, w = y.shape
        cells.append(y[: h // 9 * 9, : w // 16 * 16].reshape(9, h // 9, 16, w // 16).mean(axis=(1, 3)).ravel())
    ok = worst_flat <= 0.25 and worst_spread >= 25
    fails += not ok
    verdict = "PASS" if ok else "FAIL"
    print(
        f"{verdict} flat: frames={len(frames)} worst flat-tile share={worst_flat:.3f} (limit 0.25) "
        f"worst p5-p95 spread={worst_spread:.1f} (limit 25)"
    )
    smooth = smooth_sum / len(frames)
    ok = smooth <= 0.25
    fails += not ok
    print(f"{'PASS' if ok else 'FAIL'} smooth: mean share of tiles with std < 3/255 = {smooth:.3f} (limit 0.25)")
    # Dendrite layer and screen coverage: measured on PNG frame directories.
    # A branching layer is a dense set of thin, high-contrast fingers in every
    # orientation, so `edges` (and `spread` with it) is low for the smooth lamp
    # looks and high for the dendrite presets; `coverage` is the share of the
    # frame that is not background.
    dend = [dendrite_metrics(rgb_frame(f)) for f in sorted(d.glob("*.png"))] if not d.is_file() else []
    if dend:
        edges = float(np.mean([m[0] for m in dend]))
        spread = float(np.mean([m[1] for m in dend]))
        cov = float(np.mean([m[2] for m in dend]))
        print(
            f"INFO dendrite: frames={len(dend)} edges={edges:.4f} spread={spread:.3f} "
            f"score={edges:.4f} coverage={cov:.3f}"
        )
        if "--dendrite-min" in sys.argv:
            lim = float(sys.argv[sys.argv.index("--dendrite-min") + 1])
            ok = edges >= lim
            fails += not ok
            print(f"{'PASS' if ok else 'FAIL'} dendrite: edges={edges:.4f} (min {lim})")
        if "--dendrite-max" in sys.argv:
            lim = float(sys.argv[sys.argv.index("--dendrite-max") + 1])
            ok = edges <= lim
            fails += not ok
            print(f"{'PASS' if ok else 'FAIL'} dendrite: edges={edges:.4f} (max {lim})")
        if "--coverage-min" in sys.argv:
            lim = float(sys.argv[sys.argv.index("--coverage-min") + 1])
            ok = cov >= lim
            fails += not ok
            print(f"{'PASS' if ok else 'FAIL'} coverage: {cov:.3f} (min {lim})")
    if "--dendrite-only" in sys.argv:
        return 1 if fails else 0
    if "--flat-only" in sys.argv:
        return 1 if fails else 0
    if len(series) < int(3 * hz):
        print("FAIL flash: need at least 3 s of frames, have %.1f s" % (len(series) / hz))
        return 1
    x = np.column_stack([np.array(series)] + [np.array(cells)])
    k = max(1, round(hz))
    kernel = np.ones(k) / k
    worst, worst_f = 0.0, 0.0
    for col in x.T:
        trend = np.convolve(np.pad(col, (k // 2, k - 1 - k // 2), mode="edge"), kernel, mode="valid")
        r = col - trend
        spec = np.abs(np.fft.rfft(r * np.hanning(len(r)))) * 2.0 / (np.hanning(len(r)).sum())
        freqs = np.fft.rfftfreq(len(r), 1.0 / hz)
        band = freqs > 3.0
        if band.any():
            i = int(np.argmax(spec[band]))
            if spec[band][i] > worst:
                worst, worst_f = float(spec[band][i]), float(freqs[band][i])
    ok = worst <= 4.0
    fails += not ok
    print(
        "{} flash: {:.1f} s at {:.1f} Hz, worst component above 3 Hz = {:.2f}/255 at {:.1f} Hz (limit 4)".format(
            "PASS" if ok else "FAIL", len(series) / hz, hz, worst, worst_f
        )
    )
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
