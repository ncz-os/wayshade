#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""magmasimplex-footage-stats.py - measure wax-lamp statistics from a
video of real lamps, to calibrate the goo simulation (docs/MAGMASIMPLEX-60S.md).

Analysis only: the footage is read from a local path and nothing from it is
stored except the numbers printed. Lamp geometry (globe position and taper)
and the wax colour test are given per lamp in a small table below; the
defaults fit the three-lamp real-time recording the operator supplied
(640x360).

For each sampled frame and lamp: mask the wax by colour inside the globe,
label connected components (4-connected flood fill), classify each as pool
(touches the bottom band), cap (touches the top band) or free; track free
components frame to frame by nearest centroid; report per lamp:
  - free masses on screen: mean, histogram, share of seconds with none;
  - blob size: equivalent diameter as a fraction of the globe width there;
  - aspect ratio (height / width of the bounding box);
  - vertical speed of rising and sinking tracks, in globe heights per s;
  - transit time bottom band -> top band (and back) for tracked masses;
  - busy/quiet: free masses per minute over the window.

Usage: magmasimplex-footage-stats.py VIDEO START_S DURATION_S [FPS]
"""

import subprocess
import sys
from collections import deque

import numpy as np

W, H = 640, 360
# name: centre x, top y, bottom y, half width at top, half width at bottom
LAMPS = {
    "L1-red-in-amber": (122, 60, 198, 20, 40),
    "L2-cyan-in-blue": (247, 60, 198, 20, 40),
    "L3-pink-in-teal": (370, 60, 198, 20, 40),
}


def wax_mask(name, rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    if name.startswith("L1"):  # amber liquid: wax is red or blown-out white
        return (b > 170) | ((g < 130) & (r > 200))
    if name.startswith("L2"):  # blue liquid: wax is cyan-white
        return (g > 150) & (r > 60)  # the lit cyan liquid near the top has r ~ 0
    return (b > 195) | (b - g > 90)  # teal liquid: wax is violet/pink/white


def globe(name):
    cx, y0, y1, h0, h1 = LAMPS[name]
    ys = np.arange(y0, y1)
    hw = h0 + (h1 - h0) * (ys - y0) / float(y1 - y0)
    xs = np.arange(cx - h1, cx + h1)
    inside = np.abs(xs[None, :] - cx) < (hw[:, None] - 3)
    return (y0, y1, cx - h1, cx + h1), inside, hw


def components(mask):
    h, w = mask.shape
    lab = np.zeros((h, w), np.int32)
    comps = []
    n = 0
    for y in range(h):
        row = mask[y]
        for x in np.nonzero(row & (lab[y] == 0))[0]:
            if lab[y, x]:
                continue
            n += 1
            q = deque([(y, x)])
            lab[y, x] = n
            pts = []
            while q:
                cy, cx = q.popleft()
                pts.append((cy, cx))
                for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not lab[ny, nx]:
                        lab[ny, nx] = n
                        q.append((ny, nx))
            a = np.array(pts)
            comps.append(a)
    return comps


def main():
    video, start, dur = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
    fps = float(sys.argv[4]) if len(sys.argv) > 4 else 2.0
    cmd = [
        "ffmpeg",
        "-loglevel",
        "error",
        "-ss",
        str(start),
        "-t",
        str(dur),
        "-i",
        video,
        "-vf",
        f"fps={fps:g}",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgb24",
        "-",
    ]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    geo = {k: globe(k) for k in LAMPS}
    st = {k: {"free": [], "sizes": [], "aspects": [], "tracks": [], "live": []} for k in LAMPS}
    fi = 0
    while True:
        buf = p.stdout.read(W * H * 3)
        if len(buf) < W * H * 3:
            break
        img = np.frombuffer(buf, np.uint8).reshape(H, W, 3).astype(np.int16)
        t = fi / fps
        for k in LAMPS:
            (y0, y1, x0, x1), inside, hw = geo[k]
            sub = img[y0:y1, x0:x1]
            m = wax_mask(k, sub) & inside
            gh = y1 - y0
            frees = []
            threads = 0
            for a in components(m):
                if len(a) < 6:
                    continue
                ymin, ymax = a[:, 0].min(), a[:, 0].max()
                if ymax >= gh - 10 or ymin <= 4:  # pool band / cap band
                    continue
                cy, cx = a[:, 0].mean(), a[:, 1].mean()
                width = hw[int(cy)] * 2.0
                d = 2.0 * np.sqrt(len(a) / np.pi)
                bw = a[:, 1].max() - a[:, 1].min() + 1
                bh = ymax - ymin + 1
                if bh / float(bw) > 3.0 and len(a) < 60:
                    threads += 1  # thin strand or glint, not a blob
                    continue
                if len(a) < 20:
                    continue  # speck
                frees.append((cy, cx, d / width, bh / float(bw)))
            s = st[k]
            s["free"].append(len(frees))
            s.setdefault("threads", []).append(threads)
            for f in frees:
                s["sizes"].append(f[2])
                s["aspects"].append(f[3])
            # nearest-centroid tracking
            new_live = []
            used = set()
            for tr in s["live"]:
                ly, lx = tr[-1][1], tr[-1][2]
                best, bd = None, 7.0
                for j, f in enumerate(frees):
                    if j in used:
                        continue
                    dd = np.hypot(f[0] - ly, f[1] - lx)
                    if dd < bd:
                        best, bd = j, dd
                if best is None:
                    s["tracks"].append(tr)
                else:
                    used.add(best)
                    tr.append((t, frees[best][0], frees[best][1]))
                    new_live.append(tr)
            for j, f in enumerate(frees):
                if j not in used:
                    new_live.append([(t, f[0], f[1])])
            s["live"] = new_live
        fi += 1
    p.wait()
    print(f"window start={start:.0f} s duration={dur:.0f} s fps={fps:g} frames={fi}")
    for k in LAMPS:
        s = st[k]
        (y0, y1, _, _), _, _ = geo[k]
        gh = float(y1 - y0)
        tracks = s["tracks"] + s["live"]
        free = np.array(s["free"])
        up, down, transit_up, transit_down, life = [], [], [], [], []
        for tr in tracks:
            if len(tr) < 4:
                continue
            ts = np.array([q[0] for q in tr])
            ys = np.array([q[1] for q in tr])
            dt = ts[-1] - ts[0]
            life.append(dt)
            v = -(ys[-1] - ys[0]) / gh / dt  # up is positive (image y grows down)
            (up if v > 0 else down).append(abs(v))
            # Band rate: time to cross the middle 30 % of the globe, scaled to
            # a full globe height (tracks rarely survive pool to cap).
            lo, hi = gh * 0.65, gh * 0.35
            for i0 in range(len(ys)):
                for i1 in range(i0 + 1, len(ys)):
                    if ys[i0] > lo and ys[i1] < hi:
                        transit_up.append((ts[i1] - ts[i0]) / 0.3)
                        break
                    if ys[i0] < hi and ys[i1] > lo:
                        transit_down.append((ts[i1] - ts[i0]) / 0.3)
                        break
                else:
                    continue
                break
        per_min = [free[i : i + int(60 * fps)].mean() for i in range(0, len(free), int(60 * fps))]

        def q(a, ps=(10, 50, 90)):
            return "n/a" if len(a) == 0 else " ".join(f"p{pp}={np.percentile(a, pp):.3g}" for pp in ps)

        print(f"\n[{k}]")
        hist = np.bincount(free, minlength=6)[:8].tolist()
        print(f" free masses: mean={free.mean():.2f}  none={100.0 * (free == 0).mean():.1f}%  hist={hist}")
        print(" diameter/globe width: {}".format(q(s["sizes"])))
        print(" aspect h/w: {}".format(q(s["aspects"])))
        print(f" rise speed (globe heights/s): {q(up)}  (tracks {len(up)})")
        print(f" sink speed (globe heights/s): {q(down)}  (tracks {len(down)})")
        print(f" rise time per globe height, mid-band crossings (s): {q(transit_up)}  (n {len(transit_up)})")
        print(f" sink time per globe height, mid-band crossings (s): {q(transit_down)}  (n {len(transit_down)})")
        print(" thin strands/glints per frame: mean={:.2f}".format(np.mean(s["threads"])))
        print(f" track lifetime (s): {q(life)}")
        quiet = sum(1 for v in per_min if v < 0.2)
        print(
            f" free masses per minute: min={min(per_min):.2f} p50={float(np.median(per_min)):.2f} "
            f"max={max(per_min):.2f}  quiet minutes (<0.2)={quiet} of {len(per_min)}"
        )


if __name__ == "__main__":
    main()
