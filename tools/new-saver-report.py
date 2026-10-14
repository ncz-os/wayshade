#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""new-saver-report.py - markdown measurement tables from new-saver-test.py results.

usage: new-saver-report.py LABEL=RESULTS_DIR [LABEL=RESULTS_DIR ...] > table.md

One row per saver per run (label, plus @tag for tagged runs such as an offload-off
pass). Cells are fps / p95 / p99 ms; a run that met the GPU-class rule (>= 30 fps with
p95 <= 40 ms, tools/measure-tiers.py) is marked "ok". A software renderer invalidates
the row.
"""

import json
import pathlib
import sys


def cell(r):
    if not r:
        return "-"
    if "error" in r and r.get("fps") is None:
        return "ERR"
    if r.get("software"):
        return "SOFTWARE (invalid)"
    f = r.get("fps")
    if f is None:
        return "no frames"
    mark = " ok" if r.get("meets_30fps_p95_40") else ""
    return f"{f:g} / {r.get('p95_ms')} / {r.get('p99_ms')}{mark}"


def main():
    rows = []
    for spec in sys.argv[1:]:
        label, d = spec.split("=", 1)
        rj = pathlib.Path(d) / "results.json"
        if not rj.is_file():
            continue
        for key, r in json.loads(rj.read_text()).items():
            sv, _, tag = key.partition("@")
            rows.append((sv, label + (f"@{tag}" if tag else ""), r))
    rows.sort(key=lambda x: (x[0], x[1]))
    print(
        "| saver | host | renderer | GPU plan | Auto fps/p95/p99 | native 1.0 | weak 0.5 | gate | exit |"
    )
    print("|---|---|---|---|---|---|---|---|---|")
    for sv, host, r in rows:
        c = r.get("capture") or {}
        src = c or r.get("default") or r.get("native") or {}
        g = c.get("gate") or {}
        gate = (
            (
                "PASS"
                if g.get("pass")
                else "FAIL " + ",".join(g.get("visual_reasons") or [])
            )
            if g
            else "-"
        )
        if g and not g.get("pass") and not g.get("visual_reasons"):
            gate = f"FAIL (motion {g.get('motion')}, cov {g.get('coverage')})"
        plan = f"{src.get('plan_class', '-')}{' offload' if src.get('plan_offload') else ''}"
        print(
            f"| {sv.replace('_gles3', '')} | {host} | {src.get('renderer', '-')} | {plan} | {cell(r.get('default'))} | "
            f"{cell(r.get('native'))} | {cell(r.get('weak'))} | {gate} | {c.get('exit', '-')} |"
        )


if __name__ == "__main__":
    main()
