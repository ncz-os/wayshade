#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Representative-seed performance gates for the xshadertoy family, including sustained runs.

usage:
  xshadertoy-perf-gate.py bench --render BUILD/xshadertoy_render [--shaders a,b | --all]
        [--seeds 1111,4242,90210] [--scenes] [--minutes 3] [--window 60] [--size 1920x1080]
        [--scale 0.5] [--quality 0,1,2] [--budget-ms 33.3] [--max-drift 1.25] --out DIR
  xshadertoy-perf-gate.py live --bindir BUILD [--shaders ...] [--seeds ...] [--scenes]
        [--minutes 3] [--window 60] [--budget-ms 40] [--max-drift 1.25] --out DIR

bench  headless GPU time (xshadertoy_render --bench: frames back to back, glFinish each, motion
       time = wall time so the director and every slow parameter move as in a real run). The
       render size is --size times --scale (the weak-class default render scale is 0.5).
       Needs no compositor; runs on a busy desktop too, but take gate numbers with the idle
       daemon stopped and nothing else on the GPU.
live   the real screensaver binaries in the current Wayland session (WAYLAND_DISPLAY), with
       NCZ_XST_PERF_WINDOW so the driver prints frame-interval percentiles per window. This
       includes the compositor and vsync, so it is what a user sees.

Representative seeds: every seed in --seeds for each quality tier, plus (with --scenes) every
scene (preset) of the saver at its own seed 4242, because scenes push options to the ends of
their ranges where randomize never goes.

Gate per run: p95 <= --budget-ms, p99 <= 1.5 x budget, and drift <= --max-drift, where drift is
the p95 of the last window over the p95 of the first window (a sustained run must not slow down
as its structure evolves or as the GPU heats). Per window numbers are kept, so a slow minute
shows up even when the whole-run percentile passes.

Writes DIR/results.json and DIR/results.md; exit status 1 when any run fails the gate.
"""

import argparse
import json
import os
import pathlib
import re
import select
import signal
import subprocess
import sys
import time

SHADER_DIRS = ["vendor/xshadertoy/glsl", "vendor/wave2-materia/glsl"]


def savers(root, names, all_):
    out = []
    for d in SHADER_DIRS:
        for sh in sorted((root / d).glob("*.glsl")):
            if sh.stem == "xst_common":
                continue
            src = sh.read_text(errors="replace")
            if names and sh.stem not in names:
                continue
            if not names and not all_ and "//@saver" not in src:
                continue
            m = re.search(r"//@saver\s+.*?\bid=([a-z0-9-]+)", src)
            out.append((sh, m.group(1) if m else sh.stem, "//@saver" in src))
    return out


def scenes(root, sid):
    d = root / "assets/screensaver-chooser/options/presets" / f"xshadertoy_{sid}"
    return sorted(p.stem for p in d.glob("*.conf")) if d.is_dir() else []


def gate(r, budget, max_drift):
    reasons = []
    if r.get("error"):
        return [r["error"]]
    if r["p95"] > budget:
        reasons.append(f"p95 {r['p95']:.2f} > {budget:g} ms")
    if r["p99"] > 1.5 * budget:
        reasons.append(f"p99 {r['p99']:.2f} > {1.5 * budget:g} ms")
    if r["drift"] > max_drift:
        reasons.append(f"drift {r['drift']:.2f} > {max_drift:g}")
    return reasons


def run_bench(a, root, sh, sid, opts, quality, seconds):
    w, h = (int(x) for x in a.size.split("x"))
    size = f"{max(16, int(w * a.scale))}x{max(16, int(h * a.scale))}"
    cmd = [
        a.render,
        str(sh),
        "--bench",
        str(seconds),
        "--window",
        str(a.window),
        "--size",
        size,
        "--class-quality",
        str(quality),
    ] + opts
    p = subprocess.run(
        cmd,
        capture_output=True,
        check=False,
        text=True,
        errors="replace",
        cwd=root,
        timeout=seconds + 120,
    )
    if p.returncode == 77:
        return {"error": "software renderer (skipped)"}
    m = re.search(r"\[xst-bench-json\] (\{.*\})", p.stdout)
    if p.returncode != 0 or not m:
        return {"error": f"exit {p.returncode}: {p.stderr.strip()[-300:]}"}
    r = json.loads(m.group(1))
    desc = next(
        (ln for ln in p.stderr.splitlines() if ln.startswith("[xshadertoy]")), ""
    )
    r["log"] = desc
    return r


def run_live(a, root, sid, legacy_name, opts, seconds):
    exe = pathlib.Path(a.bindir) / f"xshadertoy_{legacy_name}_gles3"
    env = dict(os.environ, NCZ_XST_PERF_WINDOW=str(a.window))
    proc = subprocess.Popen(
        [str(exe)] + opts,
        cwd=root,
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        errors="replace",
        start_new_session=True,
    )
    wins, desc, lines = [], "", []
    t_end = time.time() + seconds + 5
    try:
        while time.time() < t_end:
            ready, _, _ = select.select(
                [proc.stderr], [], [], max(0.0, t_end - time.time())
            )
            if not ready:
                break
            ln = proc.stderr.readline()
            if not ln:
                if proc.poll() is not None:
                    break
                continue
            lines = (lines + [ln])[-20:]
            if ln.startswith("[xshadertoy]"):
                desc = ln.strip()
            m = re.search(
                r"\[xst-perf\] t=(\S+) window=\S+ n=(\S+) fps=(\S+) p50=(\S+) p95=(\S+) "
                r"p99=(\S+) max=(\S+)",
                ln,
            )
            if m:
                wins.append(
                    {
                        k: float(v)
                        for k, v in zip(
                            ("t", "n", "fps", "p50", "p95", "p99", "max"), m.groups()
                        )
                    }
                )
                if wins[-1]["t"] >= seconds:
                    break
    finally:
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)  # our own process group only
            try:
                proc.wait(10)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
    if not wins:
        return {"error": "no [xst-perf] windows: " + "".join(lines[-5:]).strip()[-300:]}
    n = sum(w["n"] for w in wins)
    # whole-run percentiles are not recoverable from window percentiles: report the worst window
    tot_t = sum(w["n"] / max(w["fps"], 1e-6) for w in wins)
    return {
        "frames": int(n),
        "fps": n / tot_t if tot_t > 0 else 0.0,
        "p50": max(w["p50"] for w in wins),
        "p95": max(w["p95"] for w in wins),
        "p99": max(w["p99"] for w in wins),
        "max": max(w["max"] for w in wins),
        "window_p95": [w["p95"] for w in wins],
        "drift": wins[-1]["p95"] / wins[0]["p95"]
        if len(wins) > 1 and wins[0]["p95"] > 0
        else 1.0,
        "log": desc,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["bench", "live"])
    ap.add_argument("--render", default="build/xshadertoy_render")
    ap.add_argument("--bindir", default="build")
    ap.add_argument("--root", default=".")
    ap.add_argument("--shaders", default="")
    ap.add_argument(
        "--all", action="store_true", help="also legacy shaders without //@saver"
    )
    ap.add_argument("--seeds", default="1111,4242,90210")
    ap.add_argument("--scenes", action="store_true")
    ap.add_argument("--quality", default="0,1,2")
    ap.add_argument("--minutes", type=float, default=3)
    ap.add_argument("--window", type=float, default=60)
    ap.add_argument("--size", default="1920x1080")
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--budget-ms", type=float, default=None)
    ap.add_argument("--max-drift", type=float, default=1.25)
    ap.add_argument("--host", default=os.uname().nodename)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    budget = (
        a.budget_ms
        if a.budget_ms is not None
        else (33.3 if a.mode == "bench" else 40.0)
    )
    root = pathlib.Path(a.root).resolve()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    names = [s for s in a.shaders.split(",") if s]
    seconds = int(a.minutes * 60)
    results, failed = [], 0
    for sh, sid, opted in savers(root, names, a.all):
        runs = []
        seeds = [int(s) for s in a.seeds.split(",") if s] if opted else [0]
        qualities = (
            [int(q) for q in a.quality.split(",")] if a.mode == "bench" else [-1]
        )
        for q in qualities:
            for sd in seeds:
                runs.append((q, f"seed={sd}", [f"--seed={sd}"] if opted else []))
        if a.scenes and opted:
            for sc in scenes(root, sid):
                runs.append(
                    (
                        qualities[-1] if a.mode == "bench" else -1,
                        f"scene={sc}",
                        [f"--preset={sc}", "--seed=4242"],
                    )
                )
        for q, label, opts in runs:
            t0 = time.time()
            if a.mode == "bench":
                r = run_bench(a, root, sh, sid, opts, q, seconds)
            else:
                r = run_live(a, root, sid, sh.stem, opts, seconds)
            r.update(
                {
                    "shader": sid,
                    "file": str(sh.relative_to(root)),
                    "run": label,
                    "quality": q,
                    "mode": a.mode,
                    "host": a.host,
                    "scale": a.scale,
                    "size": a.size,
                }
            )
            reasons = gate(r, budget, a.max_drift)
            r["gate"] = "pass" if not reasons else "FAIL"
            r["reasons"] = reasons
            failed += bool(reasons)
            results.append(r)
            print(
                f"{sid:20s} q={q:2d} {label:22s} "
                + (
                    f"p50={r['p50']:.2f} p95={r['p95']:.2f} p99={r['p99']:.2f} drift={r['drift']:.2f} "
                    if "p95" in r
                    else ""
                )
                + f"{r['gate']} {'; '.join(reasons)} ({time.time() - t0:.0f} s)",
                flush=True,
            )
            (out / "results.json").write_text(
                json.dumps(
                    {"budget_ms": budget, "max_drift": a.max_drift, "results": results},
                    indent=1,
                )
            )
    md = [
        f"# xshadertoy perf gate: {a.mode} on {a.host}",
        "",
        (
            f"size {a.size} x scale {a.scale:g}, {a.minutes:g} min per run, window {a.window:g} s, "
            f"budget p95 <= {budget:g} ms, p99 <= {1.5 * budget:g} ms, drift <= {a.max_drift:g}"
        ),
        "",
        "| shader | quality | run | p50 | p95 | p99 | max | drift | window p95 | gate |",
        "|---|---|---|---|---|---|---|---|---|---|",
    ]
    for r in results:
        if "p95" in r:
            wp = " ".join(f"{x:.1f}" for x in r.get("window_p95", []))
            md.append(
                f"| {r['shader']} | {r['quality']} | {r['run']} | {r['p50']:.2f} | {r['p95']:.2f} | "
                f"{r['p99']:.2f} | {r['max']:.2f} | {r['drift']:.2f} | {wp} | {r['gate']} |"
            )
        else:
            md.append(
                f"| {r['shader']} | {r['quality']} | {r['run']} | | | | | | | {r['gate']}: {r.get('error', '')} |"
            )
    (out / "results.md").write_text("\n".join(md) + "\n")
    print(f"{len(results)} runs, {failed} failed; {out}/results.md")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
