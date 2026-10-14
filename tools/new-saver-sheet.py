#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""new-saver-sheet.py - one contact sheet per saver from new-saver-test.py results.

usage: new-saver-sheet.py OUT_DIR HOST=RESULTS_DIR [HOST=RESULTS_DIR ...] [--savers a,b]

Rows are hosts (and host@tag runs such as build-host@igpu), columns the t=5/20/45 s
screenshots. Each row is captioned with the renderer, the Auto-settings fps/p95/p99,
the GPU class the launcher planned, and the coverage/motion gate.
"""

import json
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

W = 480  # thumbnail width


def font(size):
    for p in (
        "/System/Library/Fonts/Menlo.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    ):
        try:
            return ImageFont.truetype(p, size)
        except OSError:
            continue
    return ImageFont.load_default()


def fmt(r):
    if not r or "error" in r and "fps" not in r:
        return "n/a" if not r else f"ERR {r.get('error', '')[:40]}"
    f = r.get("fps")
    return f"{f if f is not None else '-'} fps p95 {r.get('p95_ms', '-')} p99 {r.get('p99_ms', '-')} ms"


def main():
    args = sys.argv[1:]
    sel = None
    if "--savers" in args:
        i = args.index("--savers")
        sel = args[i + 1].split(",")
        del args[i : i + 2]
    out = pathlib.Path(args[0])
    out.mkdir(parents=True, exist_ok=True)
    runs = []
    for spec in args[1:]:
        host, d = spec.split("=", 1)
        d = pathlib.Path(d)
        rj = d / "results.json"
        if rj.is_file():
            runs.append((host, d, json.loads(rj.read_text())))
    savers = sel or sorted({k.split("@")[0] for _, _, res in runs for k in res})
    f_t, f_s = font(22), font(15)
    for sv in savers:
        rows = []
        for host, d, res in runs:
            for key, r in res.items():
                if key.split("@")[0] != sv or "capture" not in r:
                    continue
                tag = key.split("@")[1] if "@" in key else ""
                rows.append((host + (f"@{tag}" if tag else ""), d / key / "capture", r))
        if not rows:
            continue
        th = int(W * 9 / 16)
        cap_h = 70
        sheet = Image.new(
            "RGB", (W * 3 + 40, 50 + len(rows) * (th + cap_h + 10)), (18, 18, 20)
        )
        dr = ImageDraw.Draw(sheet)
        dr.text((10, 12), f"{sv}", fill=(240, 240, 240), font=f_t)
        y = 50
        for label, cdir, r in rows:
            c = r["capture"]
            for i, t in enumerate(("t05", "t20", "t45")):
                p = cdir / f"{t}.png"
                x = 10 + i * (W + 10)
                if p.is_file():
                    im = Image.open(p).convert("RGB")
                    im.thumbnail((W, th))
                    sheet.paste(im, (x, y))
                    dr.text(
                        (x + 6, y + 4),
                        t.replace("t0", "t=").replace("t2", "t=2").replace("t4", "t=4")
                        + " s",
                        fill=(255, 255, 0),
                        font=f_s,
                    )
                else:
                    dr.rectangle([x, y, x + W, y + th], outline=(200, 60, 60))
                    dr.text(
                        (x + 10, y + 10), f"{t}: no frame", fill=(255, 80, 80), font=f_s
                    )
            g = c.get("gate", {})
            sw = " SOFTWARE-RENDER(INVALID)" if c.get("software") else ""
            line1 = (
                f"{label}: {c.get('renderer', '?')}{sw}  class={c.get('plan_class')} "
                f"offload={c.get('plan_offload')}  exit={c.get('exit')}"
            )
            line2 = f"Auto: {fmt(r.get('default'))}   native: {fmt(r.get('native'))}   weak(0.5): {fmt(r.get('weak'))}"
            line3 = (
                f"gate {'PASS' if g.get('pass') else 'FAIL'} cov={g.get('coverage')} motion={g.get('motion')} "
                f"reasons={g.get('visual_reasons')}  alive={c.get('alive_at')}"
            )
            for j, ln in enumerate((line1, line2, line3)):
                dr.text(
                    (10, y + th + 4 + j * 20),
                    ln,
                    fill=(220, 220, 220) if j else (140, 220, 255),
                    font=f_s,
                )
            y += th + cap_h + 10
        dest = out / f"{sv}.jpg"
        sheet.save(dest, quality=85)
        print(dest)


if __name__ == "__main__":
    main()
