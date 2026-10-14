#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""magmasimplex-beat-gates.py -- motion gate for the beat layer.

Two checks, both computed from a sequence of captured frames:

  dynamics rise: the mean frame-to-frame difference energy over 20 s is clearly
      higher with dynamics 0.7 than with dynamics 0;
  periodicity: the 1 s moving average of that difference energy shows a peak in
      its autocorrelation within 10 % of the beat period for the BPM.

Frames are read from a directory (PPM P5/P6 or non-interlaced 8-bit PNG), in
numeric order. `--selftest` synthesises sequences and verifies the metric
itself, so the gate can be trusted without a GPU.

usage:
  magmasimplex-beat-gates.py --selftest
  magmasimplex-beat-gates.py --fps 60 FRAMES_DIR [--bpm 110] [--csv out.csv]
  magmasimplex-beat-gates.py --fps 60 --baseline D0 --dynamic D7 --bpm 110
"""

import argparse
import pathlib
import struct
import sys
import zlib

PPM_MAGIC = (b"P5", b"P6")
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


class FrameError(Exception):
    pass


def read_ppm(data):
    """Return (width, height, luminance[list of 0..255])."""
    fields = []
    i = 2
    while len(fields) < 3:
        while i < len(data) and data[i : i + 1].isspace():
            i += 1
        if data[i : i + 1] == b"#":
            while i < len(data) and data[i : i + 1] != b"\n":
                i += 1
            continue
        j = i
        while j < len(data) and not data[j : j + 1].isspace():
            j += 1
        fields.append(int(data[i:j]))
        i = j
    w, h, maxval = fields
    if maxval != 255:
        raise FrameError("only 8-bit PPM is supported")
    i += 1
    chans = 1 if data[:2] == b"P5" else 3
    need = w * h * chans
    px = data[i : i + need]
    if len(px) < need:
        raise FrameError("truncated PPM")
    if chans == 1:
        return w, h, list(px)
    lum = [0] * (w * h)
    for k in range(w * h):
        r, g, b = px[3 * k], px[3 * k + 1], px[3 * k + 2]
        lum[k] = (2126 * r + 7152 * g + 722 * b) // 10000
    return w, h, lum


def read_png(data):
    """Minimal non-interlaced 8-bit PNG decoder; returns (w, h, luminance)."""
    pos = 8
    w = h = bitdepth = colortype = interlace = None
    idat = bytearray()
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        ctype = data[pos + 4 : pos + 8]
        body = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            w, h, bitdepth, colortype, _c, _f, interlace = struct.unpack(">IIBBBBB", body)
        elif ctype == b"IDAT":
            idat += body
        elif ctype == b"IEND":
            break
    if bitdepth != 8 or interlace != 0:
        raise FrameError("only non-interlaced 8-bit PNG is supported")
    chans = {0: 1, 2: 3, 4: 2, 6: 4}.get(colortype)
    if chans is None:
        raise FrameError(f"unsupported PNG colour type {colortype}")
    raw = zlib.decompress(bytes(idat))
    stride = w * chans
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        ftype = raw[p]
        p += 1
        line = bytearray(raw[p : p + stride])
        p += stride
        for x in range(stride):
            a = line[x - chans] if x >= chans else 0
            b = prev[x]
            c = prev[x - chans] if x >= chans else 0
            if ftype == 1:
                line[x] = (line[x] + a) & 0xFF
            elif ftype == 2:
                line[x] = (line[x] + b) & 0xFF
            elif ftype == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 0xFF
            elif ftype == 4:
                pa = abs(b - c)
                pb = abs(a - c)
                pc = abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        out[y * stride : (y + 1) * stride] = line
        prev = line
    lum = [0] * (w * h)
    for k in range(w * h):
        base = k * chans
        if chans == 1 or chans == 2:
            lum[k] = out[base]
        else:
            r, g, b = out[base], out[base + 1], out[base + 2]
            lum[k] = (2126 * r + 7152 * g + 722 * b) // 10000
    return w, h, lum


def read_frame(path):
    data = pathlib.Path(path).read_bytes()
    if data[:2] in PPM_MAGIC:
        return read_ppm(data)
    if data[:8] == PNG_MAGIC:
        return read_png(data)
    raise FrameError(f"{path}: unknown image format")


def frame_paths(directory):
    exts = {".ppm", ".pgm", ".png", ".pnm"}
    files = [p for p in pathlib.Path(directory).iterdir() if p.suffix.lower() in exts]
    return sorted(files)


def metrics(frames):
    """Per-frame mean luminance and (from the second frame) diff energy."""
    means = []
    diffs = []
    prev = None
    for _w, _h, lum in frames:
        means.append(sum(lum) / len(lum))
        if prev is not None:
            n = min(len(lum), len(prev))
            s = 0
            for k in range(n):
                d = lum[k] - prev[k]
                s += d if d >= 0 else -d
            diffs.append(s / n)
        prev = lum
    return means, diffs


def moving_average(values, window):
    if window <= 1 or len(values) < window:
        return list(values)
    out = []
    acc = sum(values[:window])
    out.append(acc / window)
    for i in range(window, len(values)):
        acc += values[i] - values[i - window]
        out.append(acc / window)
    return out


def autocorr_peak(series, min_lag, max_lag):
    """Return (lag, strength) of the best nonzero autocorrelation lag."""
    n = len(series)
    mean = sum(series) / n
    best_lag, best = 0, -1e18
    var = sum((v - mean) ** 2 for v in series)
    if var <= 1e-12:
        return 0, 0.0
    for lag in range(min_lag, min(max_lag, n - 1) + 1):
        c = 0.0
        for i in range(lag, n):
            c += (series[i] - mean) * (series[i - lag] - mean)
        c /= var
        if c > best:
            best, best_lag = c, lag
    return best_lag, best


def gate_dirs(baseline_dir, dynamic_dir, fps, bpm):
    b_frames = [read_frame(p) for p in frame_paths(baseline_dir)]
    d_frames = [read_frame(p) for p in frame_paths(dynamic_dir)]
    if len(b_frames) < 60 or len(d_frames) < 60:
        raise FrameError("need at least 60 frames in each directory")
    _bm, bd = metrics(b_frames)
    _dm, dd = metrics(d_frames)
    # 20 s window
    n = min(len(bd), len(dd), 20 * fps)
    b_mean = sum(bd[:n]) / n
    d_mean = sum(dd[:n]) / n
    ratio = d_mean / b_mean if b_mean > 1e-9 else float("inf")
    # periodicity on the 1 s moving average of the dynamic sequence
    ma = moving_average(dd, fps)
    period = 60.0 / bpm * fps if bpm else 0
    lag, _strength = (0, 0.0)
    if period:
        lag, _strength = autocorr_peak(ma, max(1, int(period * 0.5)), int(period * 1.5) + 1)
    ok_ratio = ratio >= 1.3
    ok_period = (not period) or (lag and abs(lag - period) <= 0.10 * period)
    print(f"dynamics 0.0 mean diff energy: {b_mean:.4f}")
    print(f"dynamics 0.7 mean diff energy: {d_mean:.4f}")
    print(f"ratio 0.7/0.0: {ratio:.3f}  gate >= 1.3: {'PASS' if ok_ratio else 'FAIL'}")
    if period:
        verdict = "PASS" if ok_period else "FAIL"
        print(
            f"autocorr peak lag {lag} frames ({lag / fps:.3f} s) vs beat period "
            f"{period:.1f} frames ({period / fps:.3f} s): {verdict}"
        )
    return ok_ratio and ok_period


def synthetic_sequence(fps, seconds, period_s, amplitude, seed, size=48):
    """Frames of constant base with a periodic uniform brightness pulse."""
    import random

    rnd = random.Random(seed)
    frames = []
    n = int(seconds * fps)
    pw, ph = size, size
    for i in range(n):
        phase = (i / fps) % period_s
        pulse = amplitude * max(0.0, 1.0 - phase / (period_s * 0.25))
        base = 60 + pulse
        frame = []
        for _ in range(pw * ph):
            v = int(base + rnd.uniform(-4, 4))
            frame.append(0 if v < 0 else min(v, 255))
        frames.append((pw, ph, frame))
    return frames


def selftest():
    fps = 60
    bpm = 110
    period = 60.0 / bpm
    quiet = synthetic_sequence(fps, 8, period, 0, 1)
    loud = synthetic_sequence(fps, 8, period, 60, 2)
    _qm, qd = metrics(quiet)
    _lm, ld = metrics(loud)
    q = sum(qd) / len(qd)
    l = sum(ld) / len(ld)
    ratio = l / q if q > 1e-9 else float("inf")
    ma = moving_average(ld, fps)
    lag, strength = autocorr_peak(ma, int(period * fps * 0.5), int(period * fps * 1.5))
    lag_s = lag / fps
    ok_ratio = ratio >= 1.3
    ok_period = abs(lag_s - period) <= 0.10 * period
    print(f"selftest: quiet={q:.3f} loud={l:.3f} ratio={ratio:.2f} (PASS>=1.3: {ok_ratio})")
    print(
        f"selftest: autocorr peak {lag_s:.4f} s vs period {period:.4f} s, "
        f"strength {strength:.2f} (PASS: {ok_period})"
    )
    # A longer pulse train at a different BPM must move the peak.
    period2 = 60.0 / 150
    s2 = synthetic_sequence(fps, 8, period2, 60, 3)
    _m2, d2 = metrics(s2)
    lag2, _ = autocorr_peak(moving_average(d2, fps), int(period2 * fps * 0.5), int(period2 * fps * 1.5))
    ok2 = abs(lag2 / fps - period2) <= 0.10 * period2
    print(f"selftest: 150 BPM peak {lag2 / fps:.4f} s vs {period2:.4f} s (PASS: {ok2})")
    if ok_ratio and ok_period and ok2:
        print("selftest: PASS")
        return 0
    print("selftest: FAIL")
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("frames", nargs="?", help="directory of captured frames")
    ap.add_argument("--baseline", help="frames at dynamics 0")
    ap.add_argument("--dynamic", help="frames at dynamics 0.7")
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--bpm", type=float, default=0)
    ap.add_argument("--csv", help="write per-frame diff energy here")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if args.baseline and args.dynamic:
        return 0 if gate_dirs(args.baseline, args.dynamic, args.fps, args.bpm) else 1
    if not args.frames:
        ap.error("give FRAMES_DIR, --baseline/--dynamic, or --selftest")
    frames = [read_frame(p) for p in frame_paths(args.frames)]
    means, diffs = metrics(frames)
    if args.csv:
        with open(args.csv, "w") as fh:
            fh.write("frame,mean_luma,diff_energy\n")
            for i in range(len(means)):
                d = diffs[i - 1] if i > 0 else 0.0
                fh.write(f"{i},{means[i]:.6f},{d:.6f}\n")
    period = 60.0 / args.bpm * args.fps if args.bpm else 0
    if period:
        lag, strength = autocorr_peak(moving_average(diffs, args.fps), max(1, int(period * 0.5)), int(period * 1.5) + 1)
        print(f"autocorr peak {lag} frames ({lag / args.fps:.3f} s), period {period / args.fps:.3f} s, strength {strength:.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
