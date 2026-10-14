#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Before/after contact sheet for one xshadertoy shader, from exact-time headless renders.

usage: xshadertoy-contact-sheet.py --shader NEW.glsl [--before OLD.glsl] --out SHEET.jpg
           [--render build/xshadertoy_render] [--seed 4242] [--times 5,20,45,90,150,240]
           [--size 960x540] [--thumb 320] [--title TEXT] [--opt name=value ...]

Both rows use the same seed (the old shader receives the same iSeed vec4 as the new one, so a
shader that reads iSeed composes the same way). Frames come from xshadertoy_render, which uses
the driver's own source assembly, options, palette and director, so a frame at t equals a live
frame at t for the same seed and options.

Also prints motion metrics per row (mean absolute 8-bit difference):
  strobe  frame t=5 s against t=5 s + 1/60 s (should be small: no flicker)
  move5   t=0 against t=5 s (silhouette-scale motion within five seconds)
  evolve  t=45 s against t=240 s or the last time (structural change over minutes)
"""

import argparse
import pathlib
import re
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont


def render(binary, shader, outdir, tag, times, size, seed, opts, iseed=None):
    cmd = [
        binary,
        shader,
        "--out",
        str(outdir),
        "--tag",
        tag,
        "--times",
        ",".join(f"{t:g}" for t in times),
        "--size",
        size,
    ]
    if seed:
        cmd.append(f"--seed={seed}")
    if iseed:
        cmd += ["--iseed", iseed]
    cmd += [f"--{o}" for o in opts]
    p = subprocess.run(
        cmd, capture_output=True, check=False, text=True, errors="replace", timeout=600
    )
    if p.returncode != 0:
        sys.exit(f"render failed ({p.returncode}): {' '.join(cmd)}\n{p.stderr[-2000:]}")
    desc = next(
        (ln for ln in p.stderr.splitlines() if ln.startswith("[xshadertoy]")), ""
    )
    mi = re.search(r"iSeed=([^,\s]+),([^,\s]+),([^,\s]+),([^,\s]+)", p.stderr)
    frames = {}
    for t in times:
        f = outdir / f"{tag}_t{t:07.2f}.png"
        frames[t] = Image.open(f).convert("RGB")
    return (
        frames,
        desc,
        (",".join(f"{float(g):.9g}" for g in mi.groups()) if mi else None),
    )


def mad(a, b):
    return float(np.abs(np.asarray(a, np.float32) - np.asarray(b, np.float32)).mean())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shader", required=True)
    ap.add_argument("--before")
    ap.add_argument("--out", required=True)
    ap.add_argument("--render", default="build/xshadertoy_render")
    ap.add_argument("--seed", type=int, default=4242)
    ap.add_argument("--times", default="5,20,45,90,150,240")
    ap.add_argument("--size", default="960x540")
    ap.add_argument("--thumb", type=int, default=320)
    ap.add_argument("--title", default="")
    ap.add_argument("--opt", action="append", default=[])
    a = ap.parse_args()
    times = [float(t) for t in a.times.split(",")]
    extra = sorted({0.0, 5.0, 5.0 + 1 / 60, 45.0, times[-1]} - set(times))
    all_t = sorted(set(times) | set(extra))
    out = pathlib.Path(a.out)
    work = out.parent / (out.stem + ".frames")
    work.mkdir(parents=True, exist_ok=True)

    rows = []
    # the old shader gets the same iSeed vec4 the new one got
    after, desc, iseed = render(
        a.render, a.shader, work, "after", all_t, a.size, a.seed, a.opt
    )
    if a.before:
        before, _, _ = render(
            a.render, a.before, work, "before", all_t, a.size, 0, [], iseed
        )
        rows.append(("before", before))
    rows.append(("after", after))

    tw = a.thumb
    th = int(tw * after[times[0]].height / after[times[0]].width)
    pad, lab, head = 6, 16, 40
    W = pad + len(times) * (tw + pad) + 70
    H = head + len(rows) * (th + lab + pad) + 18 * len(rows) + pad
    sheet = Image.new("RGB", (W, H), (18, 18, 20))
    d = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("DejaVuSansMono.ttf", 13)
        big = ImageFont.truetype("DejaVuSansMono.ttf", 16)
    except OSError:
        font = big = ImageFont.load_default()
    d.text(
        (pad, 6), a.title or pathlib.Path(a.shader).stem, fill=(235, 235, 235), font=big
    )
    d.text(
        (pad, 24),
        (desc[:170] + "...") if len(desc) > 170 else desc,
        fill=(150, 150, 160),
        font=font,
    )
    y = head
    for name, frames in rows:
        d.text((W - 64, y + th // 2), name, fill=(220, 200, 120), font=font)
        for i, t in enumerate(times):
            x = pad + i * (tw + pad)
            sheet.paste(frames[t].resize((tw, th), Image.LANCZOS), (x, y + lab))
            d.text((x + 2, y), f"t={t:g} s", fill=(240, 220, 90), font=font)
        strobe = mad(frames[5.0], frames[5.0 + 1 / 60])
        move5 = mad(frames[0.0], frames[5.0])
        evolve = mad(frames[45.0], frames[times[-1]])
        line = f"{name}: strobe(1/60 s)={strobe:.2f}  move5(0->5 s)={move5:.2f}  evolve(45->{times[-1]:g} s)={evolve:.2f}"
        print(line)
        d.text((pad, y + lab + th + 2), line, fill=(170, 200, 170), font=font)
        y += th + lab + pad + 18
    sheet.save(out, quality=88)
    print(f"sheet {out}")


if __name__ == "__main__":
    main()
