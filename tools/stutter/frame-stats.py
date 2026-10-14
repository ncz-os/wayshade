#!/usr/bin/env python3
"""Summarize a ncz_gltimer .frames file: fps, frame-interval percentiles and
hitch counts. Usage: frame-stats.py TAG.frames [skip_frames=30] [hitch_ms=25]"""
import sys


def stats(path, skip=30, hitch=25.0):
    rows = []
    with open(path) as fh:
        for line in fh:
            parts = line.split()
            if len(parts) == 3:
                rows.append(tuple(float(x) for x in parts))
    rows = rows[skip:]
    if len(rows) < 5:
        return None
    iv = sorted(r[1] for r in rows)
    n = len(iv)
    dur = rows[-1][0] - rows[0][0]

    def q(p):
        return iv[min(n - 1, int(p * n))]

    return {
        "frames": n,
        "fps": n / dur * 1000.0 if dur > 0 else 0.0,
        "p50": q(0.50),
        "p95": q(0.95),
        "p99": q(0.99),
        "max": iv[-1],
        "hitches": sum(1 for x in iv if x > hitch),
    }


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    skip = int(argv[2]) if len(argv) > 2 else 30
    hitch = float(argv[3]) if len(argv) > 3 else 25.0
    s = stats(argv[1], skip, hitch)
    if s is None:
        print("too few frames")
        return 1
    print("n=%(frames)d fps=%(fps).1f p50=%(p50).1f p95=%(p95).1f "
          "p99=%(p99).1f max=%(max).1f hitches=%(hitches)d" % s)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
