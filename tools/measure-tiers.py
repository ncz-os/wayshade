#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build assets/screensaver-chooser/tiers.tsv from host-test perf runs.

usage: measure-tiers.py OUT.tsv --uhd630 DIR [--uhd630-scaled DIR] --mali DIR
                        [--navi14 DIR] [--rtx2060 DIR] --commit SHA

Each DIR is a host-test.sh result directory (or its host subdirectory) from a run with the
perf phase. Rule (operator requirement: weak systems show what works well on them):
  weak    the hack holds >= 30 fps with p95 frame time <= 40 ms on the Intel UHD 630 at its
          DEFAULT render scale for the weak class: 0.5 for shader hacks (--uhd630-scaled),
          native resolution for the others, AND on the Mali-G720 (a faster class must never
          run a weak-ok hack worse, so the pools stay monotone)
  mid     not weak, but the same holds on the Mali-G720 at its platform default
  strong  everything else
Columns (tab separated): id, min class, then fps/p95_ms for uhd630 (native), uhd630_scaled
(render scale 0.5; equals native for hacks that ignore the scale), mali, navi14, rtx2060
('-' when not measured), then "date commit". Missing or failed measurements count as not ok.
Without --uhd630-scaled a shader hack must pass at native resolution to be weak (conservative).
"""

import argparse
import datetime
import json
import pathlib
import sys

MIN_FPS = 30.0
MAX_P95_MS = 40.0
SHADER_PREFIXES = ("hyprsaver_", "xshadertoy_", "blackhole_")


def load(path):
    p = pathlib.Path(path)
    for c in [p / "results.json", *sorted(p.glob("*/results.json"))]:
        if c.is_file():
            return json.loads(c.read_text())["perf"]["hacks"]
    sys.exit(f"no results.json with perf data under {path}")


def cell(entry):
    if not entry or "fps" not in entry:
        return "-"
    return f"{entry['fps']:.1f}/{entry['p95_ms']:.1f}"


def ok(entry):
    return (
        bool(entry)
        and entry.get("fps", 0) >= MIN_FPS
        and entry.get("p95_ms", 1e9) <= MAX_P95_MS
    )


def classify(hid, native, scaled, mali):
    """Minimum class for one hack from its three reference measurements."""
    shader = hid.startswith(SHADER_PREFIXES)
    weak_entry = scaled if (shader and scaled is not None) else native
    if ok(weak_entry) and ok(mali):
        return "weak"
    return "mid" if ok(mali) else "strong"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=pathlib.Path)
    ap.add_argument("--uhd630", required=True)
    ap.add_argument("--uhd630-scaled")
    ap.add_argument("--mali", required=True)
    ap.add_argument("--navi14")
    ap.add_argument("--rtx2060")
    ap.add_argument("--commit", required=True)
    args = ap.parse_args()
    native = load(args.uhd630)
    scaled = load(args.uhd630_scaled) if args.uhd630_scaled else {}
    mali = load(args.mali)
    navi = load(args.navi14) if args.navi14 else {}
    rtx = load(args.rtx2060) if args.rtx2060 else {}
    ids = sorted(set(native) | set(mali))
    stamp = f"{datetime.datetime.now(tz=datetime.UTC).date().isoformat()} {args.commit}"
    lines = [
        "# Minimum GPU class per screensaver: weak = at least 30 fps with p95 <= 40 ms on the Intel UHD 630 at the weak-class",
        "# default render scale (0.5 for shader hacks, native for the rest); mid = same on the Mali-G720; strong = the rest.",
        "# Columns (tab separated): id, min class, fps/p95_ms on uhd630 native, uhd630 at scale 0.5, mali_g720, amd_navi14,",
        "# nvidia_rtx2060 ('-' = not measured), then measured date and commit. Produced by tools/measure-tiers.py.",
        "# An id may also be a preset row id from presets.tsv; unmeasured rows may be declared with '-' cells and 'declared DATE'.",
    ]
    counts = {"weak": 0, "mid": 0, "strong": 0}
    for hid in ids:
        cls = classify(
            hid, native.get(hid), scaled.get(hid) if scaled else None, mali.get(hid)
        )
        counts[cls] += 1
        cells = [
            cell(native.get(hid)),
            cell(scaled.get(hid)) if scaled else "-",
            cell(mali.get(hid)),
            cell(navi.get(hid)),
            cell(rtx.get(hid)),
        ]
        lines.append("\t".join([hid, cls, *cells, stamp]))
    args.out.write_text("\n".join(lines) + "\n")
    print(
        f"{len(ids)} hacks: {counts['weak']} weak-ok, {counts['mid']} mid-ok, {counts['strong']} strong-only -> {args.out}"
    )


if __name__ == "__main__":
    main()
