#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build per-host contact sheets from harness screenshots.

usage: make-contact-sheet.py RESULT_DIR OUT_DIR [--per-sheet 21] [--columns 7]

RESULT_DIR is one host's harness output (results.json plus shots/<id>_b.ppm). One sheet
holds a grid of thumbnails labelled with the hack id; a red frame marks a REVIEW verdict,
an orange frame a hack listed in broken.tsv, a grey frame a failure. Sheets are written as
OUT_DIR/contact-sheet-N.jpg. Prints the REVIEW ids, one per line, prefixed with "REVIEW ".
"""

import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import host_test_image as hti
from PIL import Image, ImageDraw

THUMB = (256, 144)
LABEL_H = 18
PAD = 6


def load_frame(shots: pathlib.Path, hid: str) -> Image.Image | None:
    for suffix in ("_b.ppm", "_a.ppm"):
        path = shots / f"{hid}{suffix}"
        if path.is_file():
            try:
                w, h, buf = hti.parse_ppm(path.read_bytes())
            except (OSError, hti.PPMError):
                return None
            return Image.frombytes("RGB", (w, h), bytes(buf[: w * h * 3]))
    for ext in (".jpg", ".png"):
        path = shots / f"{hid}{ext}"
        if path.is_file():
            return Image.open(path).convert("RGB")
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("result_dir", type=pathlib.Path)
    ap.add_argument("out_dir", type=pathlib.Path)
    ap.add_argument("--per-sheet", type=int, default=21)
    ap.add_argument("--columns", type=int, default=7)
    ap.add_argument(
        "--reanalyze",
        action="store_true",
        help="apply the current visual gate to old results whose frames are on disk",
    )
    ap.add_argument(
        "--broken", type=pathlib.Path, help="broken.tsv to mark known issues"
    )
    ap.add_argument(
        "--ground",
        type=pathlib.Path,
        help="ground.tsv: hacks judged by the ground profile on --reanalyze",
    )
    args = ap.parse_args()

    results = json.loads((args.result_dir / "results.json").read_text())
    hacks = sorted(results.get("hacks", []), key=lambda r: r["id"])
    shots = args.result_dir / "shots"
    known: dict[str, str] = {}
    if args.broken and args.broken.is_file():
        for ln in args.broken.read_text().splitlines():
            f = ln.split("\t")
            if ln.strip() and not ln.startswith("#") and len(f) >= 3:
                known[f[0]] = f[1]
    ground: set[str] = set()
    if args.ground and not args.ground.is_file():
        print(f"warning: --ground {args.ground} not found; no ground profile", file=sys.stderr)
    if args.ground and args.ground.is_file():
        for ln in args.ground.read_text().splitlines():
            f = ln.split("\t")
            if ln.strip() and not ln.startswith("#") and f[0]:
                ground.add(f[0])
    for hk in hacks:
        if hk["id"] in known:
            hk.setdefault("metrics", {})["known_issue"] = known[hk["id"]]
        if args.reanalyze and hk["status"] == "pass":
            a, b = shots / f"{hk['id']}_a.ppm", shots / f"{hk['id']}_b.ppm"
            if a.is_file() and b.is_file():
                _, _, fa = hti.parse_ppm(a.read_bytes())
                w, h, fb = hti.parse_ppm(b.read_bytes())
                reasons = hti.visual_verdict(
                    hti.visual_metrics(w, h, fb, fa),
                    "ground" if hk["id"] in ground else "",
                )
                if reasons:
                    hk["status"] = "review"
                    hk.setdefault("metrics", {})["visual_reasons"] = ",".join(reasons)
    if args.reanalyze:
        (args.out_dir / "reanalysis.json").parent.mkdir(parents=True, exist_ok=True)
        (args.out_dir / "reanalysis.json").write_text(
            json.dumps(
                {
                    hk["id"]: hk["metrics"].get("visual_reasons", "")
                    for hk in hacks
                    if hk["status"] == "review"
                },
                indent=1,
                sort_keys=True,
            )
            + "\n"
        )
    args.out_dir.mkdir(parents=True, exist_ok=True)
    cols = args.columns
    cell_w, cell_h = THUMB[0] + PAD, THUMB[1] + LABEL_H + PAD
    for old in args.out_dir.glob("contact-sheet-*.jpg"):
        old.unlink()
    total = 0
    for n, start in enumerate(range(0, len(hacks), args.per_sheet), 1):
        chunk = hacks[start : start + args.per_sheet]
        rows = (len(chunk) + cols - 1) // cols
        sheet = Image.new(
            "RGB", (cols * cell_w + PAD, rows * cell_h + PAD), (24, 24, 24)
        )
        draw = ImageDraw.Draw(sheet)
        for i, hk in enumerate(chunk):
            x = PAD + (i % cols) * cell_w
            y = PAD + (i // cols) * cell_h
            frame = load_frame(shots, hk["id"])
            if frame is not None:
                sheet.paste(frame.resize(THUMB, Image.BILINEAR), (x, y))
            else:
                draw.rectangle([x, y, x + THUMB[0], y + THUMB[1]], fill=(60, 60, 60))
            colour = None
            if hk["status"] == "review":
                colour = (220, 40, 40)
            elif hk["status"] == "fail":
                colour = (150, 150, 150)
            elif hk.get("metrics", {}).get("known_issue"):
                colour = (240, 150, 30)
            if colour:
                draw.rectangle(
                    [x - 2, y - 2, x + THUMB[0] + 1, y + THUMB[1] + 1],
                    outline=colour,
                    width=3,
                )
            tag = hk["id"].replace("_gles3", "")
            draw.text((x, y + THUMB[1] + 3), tag[:42], fill=(230, 230, 230))
            total += 1
            if hk["status"] == "review":
                print(f"REVIEW {hk['id']}")
        sheet.save(args.out_dir / f"contact-sheet-{n}.jpg", quality=80)
    print(f"{total} thumbnails, {n if total else 0} sheets", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
