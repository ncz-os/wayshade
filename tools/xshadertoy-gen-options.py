#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate (or --check) the option files of every xshadertoy that uses xshadertoy_opts.

usage: xshadertoy-gen-options.py --render BUILD/xshadertoy_render [--check] [--root .]

A shader opts in with //@saver and //@option lines (docs/XSHADERTOY-OPTIONS.md). For each one
under vendor/xshadertoy/glsl and vendor/wave2-materia/glsl this writes, under
assets/screensaver-chooser/options/:

  xshadertoy_<id>.tsv                 exactly `xshadertoy_render SHADER --dump-schema`
                                      (byte-identical to the screensaver binary's own dump)
  xshadertoy_<id>.json                the JSON schema (docs/OPTIONS-SCHEMA.md), with
                                      choice_groups/choice_labels for the palette enum
  presets/xshadertoy_<id>/<name>.json one per <name>.conf scene in the same directory
                                      (the .conf is what the hack reads with --preset)

--check compares instead of writing and exits 1 on any difference (a meson test runs it).
Presets are also validated: every value must be a known option and pass the hack's own
validation (the tool resolves each preset with --print-config and compares).
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys

BASIC = {"preset", "seed", "palette", "style", "speed"}
SHADER_DIRS = ["vendor/xshadertoy/glsl", "vendor/wave2-materia/glsl"]
PAL_HEADER = "src/xst_palettes.h"


def palettes(root):
    """name -> class from src/xst_palettes.h"""
    out = {}
    for m in re.finditer(
        r'\{"([a-z0-9-]+)",\s*XST_PAL_(DARK|LIGHT|MINERAL)',
        (root / PAL_HEADER).read_text(),
    ):
        out[m.group(1)] = m.group(2).lower()
    return out


def label(name):
    return " ".join(w.capitalize() for w in name.split("-"))


def typed(t, v):
    if t == "bool":
        return v == "true"
    if t == "int":
        return int(float(v)) if v != "" else 0
    if t == "float":
        return float(v) if v != "" else 0.0
    return v


def schema_json(sid, tsv, pals):
    opts = []
    for line in tsv.splitlines():
        f = line.split("\t")
        if len(f) != 10:
            sys.exit(f"{sid}: bad schema line: {line[:80]}")
        name, t, dflt, mn, mx, choices, lab, desc, group, env = f
        o = {"id": name, "type": t, "label": lab, "default": typed(t, dflt)}
        if t in ("int", "float"):
            o["range"] = [typed(t, mn), typed(t, mx)]
        if t == "enum":
            o["choices"] = choices.split(",")
        if group:
            o["group"] = group
        o["level"] = "basic" if name in BASIC else "advanced"
        if desc:
            o["description"] = desc
        o["env"] = env
        if name == "palette" and t == "enum":
            ch = o["choices"]
            groups = [
                {
                    "label": "Automatic",
                    "choices": [c for c in ch if c in ("auto", "native")],
                }
            ]
            for cls, title in (
                ("dark", "Dark grounds"),
                ("light", "Light materials"),
                ("mineral", "Stone and mineral"),
            ):
                groups.append(
                    {"label": title, "choices": [c for c in ch if pals.get(c) == cls]}
                )
            o["choice_groups"] = groups
            o["choice_labels"] = {
                c: (
                    "Drawn from the seed"
                    if c == "auto"
                    else "The saver's own colors"
                    if c == "native"
                    else label(c)
                )
                for c in ch
            }
        opts.append(o)
    return {"id": f"xshadertoy_{sid}", "options": opts}


def read_conf(path):
    meta, vals, sec = {}, {}, None
    for ln in path.read_text().splitlines():
        ln = ln.strip()
        if not ln or ln[0] in "#;":
            continue
        if ln.startswith("["):
            sec = ln.strip("[]")
            continue
        k, _, v = ln.partition("=")
        (meta if sec == "preset" else vals)[k.strip()] = v.strip()
    return meta, vals


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", required=True)
    ap.add_argument("--root", default=".")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    root = pathlib.Path(a.root).resolve()
    optdir = root / "assets/screensaver-chooser/options"
    pals = palettes(root)
    problems = []
    outputs = {}
    n = 0
    for d in SHADER_DIRS:
        for sh in sorted((root / d).glob("*.glsl")):
            src = sh.read_text(errors="replace")
            if "//@saver" not in src:
                continue
            m = re.search(r"//@saver\s+.*?\bid=([a-z0-9-]+)", src)
            sid = m.group(1) if m else sh.stem
            n += 1
            tsv = subprocess.run(
                [a.render, str(sh), "--dump-schema"],
                capture_output=True,
                check=False,
                text=True,
                errors="replace",
                cwd=root,
                timeout=60,
            )
            if tsv.returncode != 0:
                problems.append(f"{sid}: --dump-schema failed: {tsv.stderr[-300:]}")
                continue
            outputs[optdir / f"xshadertoy_{sid}.tsv"] = tsv.stdout
            outputs[optdir / f"xshadertoy_{sid}.json"] = (
                json.dumps(schema_json(sid, tsv.stdout, pals), indent=2) + "\n"
            )
            names = {ln.split("\t")[0] for ln in tsv.stdout.splitlines()}
            pdir = optdir / "presets" / f"xshadertoy_{sid}"
            confs = sorted(pdir.glob("*.conf")) if pdir.is_dir() else []
            if len(confs) < 3:
                problems.append(f"{sid}: {len(confs)} scenes, at least 3 required")
            for c in confs:
                meta, vals = read_conf(c)
                for k in vals:
                    if k not in names:
                        problems.append(f"{sid}/{c.name}: unknown option {k}")
                cfg = subprocess.run(
                    [a.render, str(sh), f"--preset={c.stem}", "--print-config"],
                    capture_output=True,
                    check=False,
                    text=True,
                    errors="replace",
                    cwd=root,
                    timeout=60,
                )
                got = dict(
                    ln.split("=", 1) for ln in cfg.stdout.splitlines() if "=" in ln
                )
                if cfg.stderr.strip():
                    problems.append(f"{sid}/{c.name}: {cfg.stderr.strip()[:200]}")
                for k, v in vals.items():
                    if k in got and got[k] != v and not _same_number(got[k], v):
                        problems.append(f"{sid}/{c.name}: {k}={v} resolves to {got[k]}")
                pj = {
                    "title": meta.get("name", c.stem),
                    "description": meta.get("description", ""),
                    "accuracy": meta.get("accuracy", "artistic"),
                    "min_class": meta.get("min-gpu-class", "weak"),
                    "values": vals,
                }
                outputs[pdir / f"{c.stem}.json"] = json.dumps(pj, indent=2) + "\n"
    for path, text in outputs.items():
        if a.check:
            if not path.is_file() or path.read_text() != text:
                problems.append(
                    f"out of date: {path.relative_to(root)} (run tools/xshadertoy-gen-options.py)"
                )
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
    for p in problems:
        print(p, file=sys.stderr)
    print(
        f"{n} savers, {len(outputs)} files{' checked' if a.check else ' written'}, {len(problems)} problems"
    )
    return 1 if problems else 0


def _same_number(a, b):
    try:
        return abs(float(a) - float(b)) < 1e-9
    except ValueError:
        return False


if __name__ == "__main__":
    sys.exit(main())
