#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""new-saver-test.py - visible-evidence runner for unshipped screensavers.

Runs on an NCZ-OS test host inside the user's Wayland session. For each saver it
launches the binary with the environment the launcher would give it (the branch
launcher module is loaded from --launcher; offload and render scale are per process
only, never ambient) and records:

  capture  screenshots at t=5/20/45 s, a ~12 s wf-recorder clip, coverage/motion/
           visual-verdict gates from two small PPM frames, the renderer string and the
           [stats] line
  default  fps / p95 / p99 at the launcher's Auto settings
  native   fps / p95 / p99 at render-scale 1.0 fixed, no height cap
  weak     fps / p95 / p99 at render-scale 0.5 fixed (the weak-class default)
  long     a >= 180 s run with a frame every 15 s, then a key press through the virtual
           keyboard; checks the process exits on input and records its exit code and
           stderr tail

A run whose renderer is llvmpipe/softpipe/software is recorded as invalid (a defect),
never as a measurement. Every process is started in its own session and stopped by
process-group id. Output: <out>/<saver>/{capture,long}/..., <out>/results.json.
"""

from __future__ import annotations

import argparse
import contextlib
import importlib.machinery
import importlib.util
import json
import os
import pathlib
import re
import signal
import subprocess
import sys
import threading
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import host_test_image as hti

SOFTWARE = re.compile(r"llvmpipe|softpipe|swrast|software", re.IGNORECASE)
FRAME_RE = re.compile(r"\[diag\] frame #(\d+)")
STATS_RE = re.compile(
    r"\[stats\].*?first_frame=(?P<ff>[\d.]+) ms frames=(?P<frames>\d+) \| first5s n=(?P<en>\d+)"
    r".*?\| steady n=(?P<sn>\d+) p50=(?P<p50>[\d.]+) p95=(?P<p95>[\d.]+) p99=(?P<p99>[\d.]+)"
    r" max=(?P<max>[\d.]+)"
)
MIN_FPS, MAX_P95 = 30.0, 40.0
EXTRA_ENV: dict[str, str] = {}
EXTRA_ARGS: list[str] = []
LIVE: set[int] = set()  # process-group ids of savers/recorders this runner started


def _on_term(signum, _frame):
    """Children run in their own sessions, so stopping the runner must stop them explicitly."""
    for pg in list(LIVE):
        with contextlib.suppress(ProcessLookupError):
            os.killpg(pg, signal.SIGKILL)
    raise SystemExit(128 + signum)


def load_launcher(path):
    loader = importlib.machinery.SourceFileLoader("ncz_launcher_branch", str(path))
    spec = importlib.util.spec_from_loader("ncz_launcher_branch", loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


class Hack:
    """One saver process: stdout+stderr captured with monotonic timestamps."""

    def __init__(self, binary, env, cwd, logpath):
        self.log = open(logpath, "w", buffering=1)  # noqa: SIM115 - lives as long as the process
        self.t0 = time.monotonic()
        self.proc = subprocess.Popen(
            [binary, *EXTRA_ARGS],
            env=env,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL,
            start_new_session=True,
            text=True,
            errors="replace",
            bufsize=1,
        )
        LIVE.add(self.proc.pid)
        self.lines: list[tuple[float, str]] = []
        self.th = threading.Thread(target=self._read, daemon=True)
        self.th.start()

    def _read(self):
        for line in self.proc.stdout:
            now = time.monotonic() - self.t0
            self.lines.append((now, line.rstrip("\n")))
            self.log.write(f"{now:8.2f} {line}")

    def alive(self):
        return self.proc.poll() is None

    def usr1(self):
        with contextlib.suppress(ProcessLookupError):
            os.kill(self.proc.pid, signal.SIGUSR1)

    def stop(self, grace=3.0):
        rc = self.proc.poll()
        if rc is None:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(self.proc.pid, signal.SIGTERM)
            try:
                rc = self.proc.wait(timeout=grace)
            except subprocess.TimeoutExpired:
                with contextlib.suppress(ProcessLookupError):
                    os.killpg(self.proc.pid, signal.SIGKILL)
                rc = self.proc.wait(timeout=5)
                rc = f"KILLED(after {grace}s SIGTERM)"
        LIVE.discard(self.proc.pid)
        self.th.join(timeout=3)
        self.log.close()
        return rc

    def text(self):
        return "\n".join(line for _, line in self.lines)

    def renderer(self):
        for _, line in self.lines:
            if line.startswith("RENDERER="):
                return line[9:].strip()
        return ""

    def stats(self):
        found = None
        for _, line in self.lines:
            m = STATS_RE.search(line)
            if m:
                found = {k: float(v) for k, v in m.groupdict().items()}
        return found

    def fps(self, since):
        pts = [
            (t, int(m.group(1)))
            for t, line in self.lines
            if (m := FRAME_RE.search(line)) and t >= since
        ]
        if len(pts) >= 2 and pts[-1][0] > pts[0][0]:
            return round((pts[-1][1] - pts[0][1]) / (pts[-1][0] - pts[0][0]), 1)
        return None


def grim(env, path, scale, ppm=False):
    cmd = ["grim", "-s", f"{scale:g}"] + (["-t", "ppm"] if ppm else []) + [str(path)]
    try:
        r = subprocess.run(cmd, env=env, capture_output=True, timeout=20, check=False)
        return r.returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def child_env(mod, hid, base_settings, cfg, stage):
    settings = dict(base_settings)
    settings["gpu-offload"] = cfg["offload"]
    env = mod.build_child_env(hid, settings)
    plan = (
        mod.plan_for(hid, settings)
        if settings["gpu-offload"] != "off"
        else mod.display_plan(settings)
    )
    scale = cfg.get("scale")
    if (
        scale is not None
    ):  # explicit, for every hack (the launcher only scales shader-prefixed ids)
        env["NCZ_RENDER_SCALE_MODE"] = "fixed"
        env["NCZ_MAX_RENDER_HEIGHT"] = "0"
        if scale < 1.0:
            env["NCZ_RENDER_SCALE"] = f"{scale:.2f}"
        else:
            env.pop("NCZ_RENDER_SCALE", None)
    env.update(EXTRA_ENV)
    env["NCZ_ASSET_DIR"] = str(stage / "shaders")
    env["NCZ_SCREENSAVER_DIRS"] = str(stage / "bin")
    env.pop("NCZ_ALLOW_SOFTWARE", None)
    env.pop("NCZ_ALLOW_SOFTWARE_FALLBACK", None)
    picked = {
        k: v
        for k, v in env.items()
        if k.startswith(
            (
                "NCZ_RENDER",
                "NCZ_MAX",
                "NCZ_GPU",
                "__NV",
                "DRI_PRIME",
                "__GLX",
                "__EGL",
                "MESA_VK",
            )
        )
    }
    gpu = plan.get("gpu") or {}
    return env, {
        "env": picked,
        "plan_class": plan.get("class"),
        "plan_offload": plan.get("offload"),
        "plan_gpu": gpu.get("name") or gpu.get("id") or str(gpu)[:80],
    }


def perf(mod, hid, binary, settings, cfg, stage, outdir, seconds, warmup):
    env, meta = child_env(mod, hid, settings, cfg, stage)
    h = Hack(binary, env, str(stage), outdir / f"perf-{cfg['name']}.log")
    deadline = time.monotonic() + warmup + seconds
    while time.monotonic() < deadline and h.alive():
        time.sleep(0.25)
    early_exit = not h.alive()
    t_usr1 = time.monotonic() - h.t0
    h.usr1()
    time.sleep(0.6)
    rc = h.stop()
    st = h.stats() or {}
    # steady-window frames / steady-window seconds from the [stats] line (the stats clock starts
    # at process start; its steady window begins 5 s later). [diag] frame stamps are a cross-check.
    fps = round(st["sn"] / (t_usr1 - 5.0), 2) if st.get("sn") and t_usr1 > 6 else None
    diag_fps = h.fps(warmup)
    if diag_fps and fps and abs(diag_fps - fps) / fps < 0.2:
        fps = diag_fps  # finer clock; the stats window edge is only approximate
    res = {
        **meta,
        "renderer": h.renderer(),
        "fps": fps,
        "p50_ms": st.get("p50"),
        "p95_ms": st.get("p95"),
        "p99_ms": st.get("p99"),
        "max_ms": st.get("max"),
        "first_frame_ms": st.get("ff"),
        "frames": st.get("frames"),
        "diag_fps": diag_fps,
        "exit": rc,
        "early_exit": early_exit,
    }
    res["software"] = bool(SOFTWARE.search(res["renderer"] or ""))
    res["meets_30fps_p95_40"] = (
        bool(fps and fps >= MIN_FPS and st.get("p95", 1e9) <= MAX_P95)
        and not res["software"]
    )
    if early_exit:
        res["tail"] = h.text()[-1500:]
    return res


def capture(mod, hid, binary, settings, cfg, stage, outdir, shot_scale, output, video):
    outdir.mkdir(parents=True, exist_ok=True)
    env_s = dict(os.environ)
    base = outdir / "baseline.ppm"
    grim(env_s, base, 0.125, ppm=True)
    env, meta = child_env(mod, hid, settings, cfg, stage)
    h = Hack(binary, env, str(stage), outdir / "hack.log")
    res = {**meta, "shots": {}, "alive_at": {}}
    rec = None

    def at(t):
        while time.monotonic() - h.t0 < t:
            time.sleep(0.05)

    for t in (5, 20):
        at(t)
        res["alive_at"][t] = h.alive()
        if t == 20:
            grim(env_s, outdir / "a.ppm", 0.125, ppm=True)
            time.sleep(1.0)
            grim(env_s, outdir / "b.ppm", 0.125, ppm=True)
        ok = grim(env_s, outdir / f"t{t:02d}.png", shot_scale)
        res["shots"][t] = ok
    if video:
        at(24)
        vcmd = [
            "wf-recorder",
            "-y",
            "-f",
            str(outdir / "clip.mp4"),
            "-c",
            "libx264",
            "-p",
            "preset=veryfast",
            "-p",
            "crf=30",
            "-F",
            "scale=1280:-2",
        ]
        if output:
            vcmd[1:1] = ["-o", output]
        rec = subprocess.Popen(
            vcmd,
            env=env_s,
            stdout=subprocess.DEVNULL,
            stderr=open(outdir / "wf-recorder.log", "w"),  # noqa: SIM115
            stdin=subprocess.DEVNULL,
            start_new_session=True,
        )
        LIVE.add(rec.pid)
        at(37)
        with contextlib.suppress(ProcessLookupError):
            os.killpg(rec.pid, signal.SIGINT)
        try:
            rec.wait(timeout=15)
        except subprocess.TimeoutExpired:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(rec.pid, signal.SIGKILL)
            rec.wait(timeout=5)
        LIVE.discard(rec.pid)
        res["video"] = (outdir / "clip.mp4").is_file() and (
            outdir / "clip.mp4"
        ).stat().st_size > 10000
    at(45)
    res["alive_at"][45] = h.alive()
    res["shots"][45] = grim(env_s, outdir / "t45.png", shot_scale)
    t_usr1 = time.monotonic() - h.t0
    h.usr1()
    time.sleep(0.6)
    res["exit"] = h.stop()
    res["renderer"] = h.renderer()
    res["software"] = bool(SOFTWARE.search(res["renderer"] or ""))
    res["stats"] = h.stats()
    st = res["stats"] or {}
    res["fps_capture"] = round(st["sn"] / (t_usr1 - 5.0), 2) if st.get("sn") else None
    try:
        _, _, a = hti.parse_ppm((outdir / "a.ppm").read_bytes())
        bw, bh, b = hti.parse_ppm((outdir / "b.ppm").read_bytes())
        _, _, z = hti.parse_ppm(base.read_bytes())
        vm = hti.visual_metrics(bw, bh, b, a)
        res["gate"] = {
            "coverage": round(hti.coverage_fraction(b), 3),
            "tile_coverage": round(hti.tile_coverage(bw, bh, b), 3),
            "motion": round(hti.frame_diff_fraction(a, b), 3),
            "baseline_diff": round(hti.frame_diff_fraction(z, b), 3),
            "visual_reasons": hti.visual_verdict(vm),
            **{
                "v_" + k: vm[k]
                for k in ("lit", "lit_colors", "lit_top1", "entropy", "moved_tiles")
            },
        }
        g = res["gate"]
        res["gate"]["nonblack"] = g["coverage"] > 0.005
        res["gate"]["moving"] = g["motion"] > 0.001 or g["v_moved_tiles"] > 0.01
        res["gate"]["pass"] = bool(
            g["nonblack"]
            and g["moving"]
            and g["baseline_diff"] > 0.05
            and not g["visual_reasons"]
        )
    except Exception as exc:  # noqa: BLE001 - evidence runner records, never aborts
        res["gate"] = {"error": str(exc), "pass": False}
    for p in ("a.ppm", "b.ppm", "baseline.ppm"):
        with contextlib.suppress(OSError):
            (outdir / p).unlink()
    res["tail"] = h.text()[-2500:]
    return res


def _thermal_max():
    best = None
    for z in pathlib.Path("/sys/class/thermal").glob("thermal_zone*/temp"):
        with contextlib.suppress(OSError, ValueError):
            v = int(z.read_text().strip()) / 1000.0
            best = v if best is None or v > best else best
    return best


def _rss_kb(pid):
    with contextlib.suppress(OSError):
        for ln in pathlib.Path(f"/proc/{pid}/status").read_text().splitlines():
            if ln.startswith("VmRSS:"):
                return int(ln.split()[1])
    return None


def _gpu_smi():
    with contextlib.suppress(
        OSError, subprocess.TimeoutExpired, ValueError, IndexError
    ):
        out = (
            subprocess.run(
                [
                    "nvidia-smi",
                    "--query-gpu=temperature.gpu,power.draw,utilization.gpu",
                    "--format=csv,noheader,nounits",
                ],
                capture_output=True,
                text=True,
                timeout=10,
                check=False,
            )
            .stdout.strip()
            .splitlines()[0]
        )
        return [float(x) for x in out.split(",")]
    return None


def soak(
    mod,
    hid,
    binary,
    settings,
    cfg,
    stage,
    outdir,
    seconds,
    shot_scale,
    wl_poke,
    exit_input,
):
    """Long soak with a telemetry sample every 60 s, then an exit-on-input check."""
    outdir.mkdir(parents=True, exist_ok=True)
    env_s = dict(os.environ)
    env, meta = child_env(mod, hid, settings, cfg, stage)
    marker = outdir / ".marker"
    marker.write_text("")
    time.sleep(1.1)
    h = Hack(binary, env, str(stage), outdir / "hack.log")
    res = {**meta, "samples": []}
    last_n = 0
    t = 60
    while t <= seconds:
        while time.monotonic() - h.t0 < t:
            time.sleep(0.2)
        if not h.alive():
            res["died_at"] = round(time.monotonic() - h.t0, 1)
            break
        stamps = [
            (tt, int(m.group(1)))
            for tt, line in h.lines[last_n:]
            if (m := FRAME_RE.search(line))
        ]
        dts = [
            float(m.group(1))
            for _, line in h.lines[last_n:]
            if (m := re.search(r"frame_t frame=\d+ dt_ms=([\d.]+)", line))
        ]
        game = [
            line
            for _, line in h.lines[last_n:]
            if " wave=" in line and " killed=" in line
        ]
        last_n = len(h.lines)
        fps = None
        if len(stamps) >= 2 and stamps[-1][0] > stamps[0][0]:
            fps = round(
                (stamps[-1][1] - stamps[0][1]) / (stamps[-1][0] - stamps[0][0]), 2
            )
        dts.sort()
        smp = {
            "t": t,
            "fps": fps,
            "rss_kb": _rss_kb(h.proc.pid),
            "thermal_max_c": _thermal_max(),
            "dt30_p50": dts[len(dts) // 2] if dts else None,
            "dt30_p95": dts[int(len(dts) * 0.95)] if dts else None,
            "dt30_max": dts[-1] if dts else None,
            "game": game[-1].split("neonspacewar ", 1)[-1] if game else None,
        }
        g = _gpu_smi() if cfg["offload"] != "off" else None
        if g:
            smp["nv_temp_c"], smp["nv_power_w"], smp["nv_util"] = g
        res["samples"].append(smp)
        if t % 300 == 0:
            grim(env_s, outdir / f"s{t:04d}.png", shot_scale * 0.5)
        t += 60
    t_usr1 = time.monotonic() - h.t0
    h.usr1()
    time.sleep(0.5)
    res["stats"] = h.stats()
    st = res["stats"] or {}
    res["fps"] = round(st["sn"] / (t_usr1 - 5.0), 2) if st.get("sn") else None
    t_in = time.monotonic()
    if exit_input == "motion":
        subprocess.run(
            [sys.executable, str(wl_poke), "motion", "--count", "3"],
            env=env_s,
            capture_output=True,
            timeout=15,
            check=False,
        )
    else:
        subprocess.run(
            [sys.executable, str(wl_poke), "key", "--code", "57"],
            env=env_s,
            capture_output=True,
            timeout=15,
            check=False,
        )
    exited = None
    while time.monotonic() - t_in < 6:
        if not h.alive():
            exited = round(time.monotonic() - t_in, 2)
            break
        time.sleep(0.02)
    res["exit_input"] = exit_input
    res["exit_on_input_s"] = exited
    res["exit"] = h.stop()
    res["leftover"] = subprocess.run(
        ["pgrep", "-x", pathlib.Path(binary).name[:15]],
        capture_output=True,
        text=True,
        check=False,
    ).stdout.split()
    res["renderer"] = h.renderer()
    res["software"] = bool(SOFTWARE.search(res["renderer"] or ""))
    home = pathlib.Path.home()
    touched = subprocess.run(
        [
            "find",
            str(home),
            "/tmp",
            "-xdev",
            "-newer",
            str(marker),
            "-type",
            "f",
            "-not",
            "-path",
            str(HERE) + "/*",
        ],
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    ).stdout.split()
    res["files_written"] = [
        f
        for f in touched
        if "shader_cache" not in f
        and "mesa" not in f.lower()
        and "nvidia" not in f.lower()
    ][:50]
    marker.unlink()
    res["tail"] = h.text()[-3000:]
    return res


def long_run(
    mod, hid, binary, settings, cfg, stage, outdir, seconds, shot_scale, wl_poke
):
    outdir.mkdir(parents=True, exist_ok=True)
    env_s = dict(os.environ)
    env, meta = child_env(mod, hid, settings, cfg, stage)
    marker = outdir / ".marker"
    marker.write_text("")
    time.sleep(1.1)
    h = Hack(binary, env, str(stage), outdir / "hack.log")
    res = {**meta, "frames": [], "alive": []}
    t = 15
    while t <= seconds:
        while time.monotonic() - h.t0 < t:
            time.sleep(0.1)
        res["alive"].append((t, h.alive()))
        if not h.alive():
            break
        if grim(env_s, outdir / f"l{t:03d}.png", shot_scale * 0.5):
            res["frames"].append(t)
        t += 15
    t_usr1 = time.monotonic() - h.t0
    h.usr1()
    time.sleep(0.5)
    res["stats"] = h.stats()
    st = res["stats"] or {}
    res["fps"] = round(st["sn"] / (t_usr1 - 5.0), 2) if st.get("sn") else None
    t_key = time.monotonic()
    subprocess.run(
        [sys.executable, str(wl_poke), "key", "--code", "57"],
        env=env_s,
        capture_output=True,
        timeout=15,
        check=False,
    )
    exited = None
    while time.monotonic() - t_key < 6:
        if not h.alive():
            exited = round(time.monotonic() - t_key, 2)
            break
        time.sleep(0.05)
    res["exit_on_key_s"] = exited
    res["exit"] = h.stop()
    res["renderer"] = h.renderer()
    res["software"] = bool(SOFTWARE.search(res["renderer"] or ""))
    res["tail"] = h.text()[-4000:]
    # Persistence check: files under $HOME (outside this runner's own dir) and /tmp touched during
    # the run. The driver shader cache is listed separately; it is driver state, not saver state.
    home = pathlib.Path.home()
    touched = subprocess.run(
        [
            "find",
            str(home),
            "/tmp",
            "-xdev",
            "-newer",
            str(marker),
            "-type",
            "f",
            "-not",
            "-path",
            str(HERE) + "/*",
        ],
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    ).stdout.split()
    res["files_written"] = [
        f
        for f in touched
        if "shader_cache" not in f
        and "mesa" not in f.lower()
        and "nvidia" not in f.lower()
    ][:50]
    res["shader_cache_files"] = len(touched) - len(res["files_written"])
    marker.unlink()
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", required=True)
    ap.add_argument("--launcher", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--savers", required=True, help="comma list of hack ids")
    ap.add_argument("--phases", default="capture,default,native,weak")
    ap.add_argument(
        "--offload", default="auto", help="gpu-offload setting: auto|off|prime"
    )
    ap.add_argument("--tag", default="", help="suffix for result keys, e.g. igpu")
    ap.add_argument("--shot-scale", type=float, default=1.0)
    ap.add_argument("--output", default="")
    ap.add_argument("--perf-seconds", type=float, default=15.0)
    ap.add_argument("--warmup", type=float, default=5.0)
    ap.add_argument("--long-seconds", type=int, default=195)
    ap.add_argument("--no-video", action="store_true")
    ap.add_argument("--soak-seconds", type=int, default=1800)
    ap.add_argument(
        "--scale",
        type=float,
        default=None,
        help="fixed render scale for the soak phase",
    )
    ap.add_argument("--exit-input", default="key", choices=["key", "motion"])
    ap.add_argument("--args", default="", help="extra saver arguments, space separated")
    ap.add_argument(
        "--env", action="append", default=[], help="KEY=VAL for the saver process only"
    )
    a = ap.parse_args()
    signal.signal(signal.SIGTERM, _on_term)
    signal.signal(signal.SIGHUP, _on_term)
    EXTRA_ENV.update(dict(kv.split("=", 1) for kv in a.env))
    EXTRA_ARGS.extend(a.args.split())
    stage = pathlib.Path(a.stage).resolve()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    mod = load_launcher(a.launcher)
    settings, _ = mod.load_settings()
    rj = out / "results.json"
    results = json.loads(rj.read_text()) if rj.is_file() else {}
    for hid in [s for s in a.savers.split(",") if s]:
        binary = str(stage / "bin" / hid)
        key = hid + (f"@{a.tag}" if a.tag else "")
        r = results.setdefault(key, {"offload_setting": a.offload})
        sd = out / key
        sd.mkdir(parents=True, exist_ok=True)
        for ph in a.phases.split(","):
            print(f"[{time.strftime('%H:%M:%S')}] {key} {ph}", flush=True)
            base = {"offload": a.offload}
            try:
                if ph == "capture":
                    r["capture"] = capture(
                        mod,
                        hid,
                        binary,
                        settings,
                        {**base, "name": "capture"},
                        stage,
                        sd / "capture",
                        a.shot_scale,
                        a.output,
                        not a.no_video,
                    )
                elif ph in ("default", "native", "weak"):
                    scale = {"default": None, "native": 1.0, "weak": 0.5}[ph]
                    r[ph] = perf(
                        mod,
                        hid,
                        binary,
                        settings,
                        {**base, "name": ph, "scale": scale},
                        stage,
                        sd,
                        a.perf_seconds,
                        a.warmup,
                    )
                elif ph == "soak":
                    r["soak"] = soak(
                        mod,
                        hid,
                        binary,
                        settings,
                        {**base, "name": "soak", "scale": a.scale},
                        stage,
                        sd / "soak",
                        a.soak_seconds,
                        a.shot_scale,
                        HERE / "wl_poke.py",
                        a.exit_input,
                    )
                elif ph == "long":
                    r["long"] = long_run(
                        mod,
                        hid,
                        binary,
                        settings,
                        {**base, "name": "long"},
                        stage,
                        sd / "long",
                        a.long_seconds,
                        a.shot_scale,
                        HERE / "wl_poke.py",
                    )
            except Exception as exc:  # noqa: BLE001
                r[ph] = {"error": repr(exc)}
            rj.write_text(json.dumps(results, indent=1, default=str))
            time.sleep(1.0)
    print("done", flush=True)


if __name__ == "__main__":
    main()
