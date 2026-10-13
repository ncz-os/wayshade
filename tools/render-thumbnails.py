#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Resize one captured frame, or build the catalogue contact sheets.

Called by tools/render-thumbnails.sh; also usable on its own:

  render-thumbnails.py frame --in FRAME.png --out THUMB.png --thumb 320x180
  render-thumbnails.py blank --in FRAME.png     # exit 0 when the frame is empty
  render-thumbnails.py sheets --catalog hacks.tsv --categories categories.tsv \
      --category-list category-list.tsv --thumbs DIR --out DIR --columns 5

`frame` centres-crops to the thumbnail aspect ratio and resizes with LANCZOS.
`sheets` writes contact-sheet-<category>.jpg, one grid of every captured
thumbnail of that style category, each cell labelled with the saver id.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont, ImageStat

LABEL_H = 22
PAD = 6
BG = (18, 18, 22)
FG = (232, 232, 236)
BORDER = (72, 72, 84)
HEADER = (14, 14, 18)


def parse_size(text: str) -> tuple[int, int]:
    w, _, h = text.partition("x")
    return int(w), int(h)


def read_tsv(path: pathlib.Path) -> list[list[str]]:
    rows: list[list[str]] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        rows.append(line.split("\t"))
    return rows


def font(size: int) -> ImageFont.ImageFont:
    for name in (
        "DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def cmd_frame(args: argparse.Namespace) -> int:
    src = Image.open(args.input).convert("RGB")
    tw, th = parse_size(args.thumb)
    scale = max(tw / src.width, th / src.height)
    if scale != 1:
        src = src.resize((max(tw, round(src.width * scale)),
                          max(th, round(src.height * scale))), Image.LANCZOS)
    left = (src.width - tw) // 2
    top = (src.height - th) // 2
    out = src.crop((left, top, left + tw, top + th))
    dst = pathlib.Path(args.output)
    dst.parent.mkdir(parents=True, exist_ok=True)
    out.save(dst, format="PNG", optimize=True)
    return 0


def cmd_blank(args: argparse.Namespace) -> int:
    """Exit 0 (true) when a frame carries no image content at all."""
    img = Image.open(args.input).convert("RGB")
    stat = ImageStat.Stat(img)
    luma = sum(stat.mean) / 3.0
    return 0 if luma < float(args.level) else 1


def cmd_sheets(args: argparse.Namespace) -> int:
    ids = [r[0] for r in read_tsv(pathlib.Path(args.catalog))]
    titles = {r[0]: r[1] for r in read_tsv(pathlib.Path(args.catalog))}
    cats = {r[0]: r[1] for r in read_tsv(pathlib.Path(args.categories))}
    labels = {r[0]: (r[1], r[2]) for r in read_tsv(pathlib.Path(args.category_list))}
    thumbs = pathlib.Path(args.thumbs)
    outdir = pathlib.Path(args.out)
    outdir.mkdir(parents=True, exist_ok=True)

    groups: dict[str, list[str]] = {}
    for hack_id in ids:
        cat = cats.get(hack_id)
        if not cat:
            continue
        short = hack_id.removesuffix("_gles3")
        if not (thumbs / f"{short}.png").is_file():
            continue
        groups.setdefault(cat, []).append(hack_id)

    tw, th = parse_size(args.thumb)
    fnt = font(13)
    hfn = font(20)
    for cat, members in sorted(groups.items()):
        label, _desc = labels.get(cat, (cat, ""))
        cols = max(1, args.columns)
        rows = (len(members) + cols - 1) // cols
        head = 34
        w = cols * (tw + PAD) + PAD
        h = head + rows * (th + LABEL_H + PAD) + PAD
        sheet = Image.new("RGB", (w, h), BG)
        draw = ImageDraw.Draw(sheet)
        draw.rectangle((0, 0, w, head), fill=HEADER)
        draw.text((PAD + 2, 8), f"{label}  -  {len(members)} screensaver"
                  f"{'s' if len(members) != 1 else ''}", font=hfn, fill=FG)
        for n, hack_id in enumerate(members):
            short = hack_id.removesuffix("_gles3")
            cx = PAD + (n % cols) * (tw + PAD)
            cy = head + (n // cols) * (th + LABEL_H + PAD)
            sheet.paste(Image.open(thumbs / f"{short}.png").convert("RGB"), (cx, cy))
            draw.rectangle((cx, cy, cx + tw - 1, cy + th - 1), outline=BORDER)
            # Two lines: the binary id (unique) and the display title (may repeat).
            draw.text((cx + 2, cy + th + 1), short, font=fnt, fill=FG)
            draw.text((cx + 2, cy + th + 12), titles.get(hack_id, ""),
                      font=fnt, fill=(150, 150, 158))
        path = outdir / f"contact-sheet-{cat}.jpg"
        sheet.save(path, format="JPEG", quality=82, optimize=True)
        print(f"contact-sheet {cat}: {len(members)} savers -> {path}")
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)

    f = sub.add_parser("frame")
    f.add_argument("--in", dest="input", required=True)
    f.add_argument("--out", dest="output", required=True)
    f.add_argument("--thumb", default="320x180")
    f.set_defaults(func=cmd_frame)

    b = sub.add_parser("blank")
    b.add_argument("--in", dest="input", required=True)
    b.add_argument("--level", default="0.5")
    b.set_defaults(func=cmd_blank)

    s = sub.add_parser("sheets")
    s.add_argument("--catalog", required=True)
    s.add_argument("--categories", required=True)
    s.add_argument("--category-list", required=True)
    s.add_argument("--thumbs", required=True)
    s.add_argument("--out", required=True)
    s.add_argument("--columns", type=int, default=5)
    s.add_argument("--thumb", default="320x180")
    s.set_defaults(func=cmd_sheets)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))