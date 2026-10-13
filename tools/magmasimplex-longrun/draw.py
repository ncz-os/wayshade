#!/usr/bin/env python3
"""draw.py - one randomized magmasimplex slice, drawn from a logged PRNG.

Emits a single TSV row (no tabs inside any field) describing the slice:

  slice  mode  category  seed  preset  palette  palette-era  world  view
  material  glitter  glitter-amount  colourway  kaleidoscope  texture  motes
  glass  depth  drift  chapter  speed  viscosity  heat  wax  cmd

`cmd` is the exact `env KEY=VALUE ... <binary>` command line that starts the
slice, so run-slice.sh can reproduce the look bit for bit.

Categories (operator review 2026-10-04/05, docs/MAGMASIMPLEX-REALISM.md):
  lamp       the lamp-faithful looks: views inside/field/glass, materials
             milky/translucent, glitter as the palette says (85 %), few motes;
  show       the ink light show (view=show), the "tie-dye" the operator kept;
  flowerchild  the overhead-projector dye show (view=butterfly; "Flowerchild");
  candy      the glossy jelly look: opt-in only, 0 % unless --candy-pct.
The category weights are arguments (defaults 70/15/15/0). Within a category,
one slice in four (slice % 4 == 3) uses one of that category's named presets
instead of a free draw.

Draw rules kept from the first driver (brief, 2026-10-04):
  * option ranges are read at run time from the schema TSV, never hard-coded;
  * float options are drawn uniformly inside the middle 15%..85% of their
    schema min..max, then intersected with the safe ranges the simulation's
    own randomize gate uses (docs/MAGMASIMPLEX-60S.md);
  * palette is drawn uniformly from every concrete lamp name in the palette
    enum, or - sometimes - palette=auto with a drawn palette-era instead.
Everything is a pure function of (base_seed, slice): the same base_seed and
weights replay the same run.
"""

import argparse
import hashlib
import math
import os
import sys

MASK = (1 << 64) - 1


def stream(base_seed, slice_no, key):
    """A stable 64-bit stream seed for one option of one slice."""
    d = hashlib.sha256(f"{base_seed}:{slice_no}:{key}".encode()).digest()[:8]
    return int.from_bytes(d, "big") & MASK


class Rng:
    """SplitMix64: stable across hosts and Python versions."""

    def __init__(self, seed):
        self.s = seed & MASK

    def next(self):
        self.s = (self.s + 0x9E3779B97F4A7C15) & MASK
        z = self.s
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
        return (z ^ (z >> 31)) & MASK

    def u01(self):
        return self.next() / 2.0**64

    def uniform(self, a, b):
        return a + (b - a) * self.u01()

    def pick(self, seq):
        return seq[self.next() % len(seq)]

    def maybe(self, p):
        return self.u01() < p

    def weighted(self, pairs):
        total = sum(w for _, w in pairs)
        r = self.u01() * total
        for v, w in pairs:
            if r < w:
                return v
            r -= w
        return pairs[-1][0]


# Safe ranges the simulation's own randomize/blob gate uses, from
# docs/MAGMASIMPLEX-60S.md; motes narrowed after the operator review (a real
# lamp has almost none).
SAFE = {
    "texture": (0.6, 1.4),
    "viscosity": (0.8, 1.1),
    "wax": (0.85, 1.1),
    "heat": (0.85, 1.15),
    "motes": (0.3, 1.0),
    "glass": (0.5, 1.5),
    "depth": (0.7, 1.3),
    "drift": (0.5, 1.5),
}
FLOAT_OPTS = [
    "texture",
    "motes",
    "glass",
    "depth",
    "drift",
    "chapter",
    "speed",
    "viscosity",
    "heat",
    "wax",
    "glitter-amount",
]
COLS = [
    "slice",
    "mode",
    "category",
    "seed",
    "pattern-seed",
    "preset",
    "palette",
    "palette-era",
    "world",
    "view",
    "material",
    "glitter",
    "glitter-amount",
    "colourway",
    "kaleidoscope",
    "texture",
    "motes",
    "glass",
    "depth",
    "drift",
    "chapter",
    "speed",
    "viscosity",
    "heat",
    "wax",
]


def parse_tsv(path):
    opts = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            c = line.split("\t")
            if len(c) < 10:
                continue
            opts[c[0]] = {
                "type": c[1],
                "default": c[2],
                "min": c[3],
                "max": c[4],
                "choices": [x for x in c[5].split(",") if x],
                "env": c[9],
            }
    return opts


def mid(lo, hi):
    return (lo + 0.15 * (hi - lo), lo + 0.85 * (hi - lo))


def draw_float(rng, name, spec):
    lo, hi = float(spec["min"]), float(spec["max"])
    a, b = mid(lo, hi)
    if name in SAFE:
        s0, s1 = SAFE[name]
        a, b = max(a, s0), min(b, s1)
    if b < a:
        a, b = mid(lo, hi)
    return f"{rng.uniform(a, b):.4f}"


def draw_int_mid(rng, spec):
    lo, hi = int(float(spec["min"])), int(float(spec["max"]))
    a, b = mid(lo, hi)
    ia, ib = math.ceil(a), math.floor(b)
    if ib < ia:
        ia, ib = lo, hi
    return str(ia + (rng.next() % (ib - ia + 1)))


def scene_values(presets_dir, name):
    """[magmasimplex] values of a scene key file; None for an alias file
    (a '# alias' comment: the old id of a renamed scene, never drawn twice)."""
    vals = {}
    sec = None
    with open(os.path.join(presets_dir, name + ".conf"), encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line.startswith("# alias"):
                return None
            if not line or line.startswith("#"):
                continue
            if line.startswith("["):
                sec = line.strip("[]")
                continue
            if sec == "magmasimplex" and "=" in line:
                k, v = line.split("=", 1)
                vals[k.strip()] = v.strip()
    return vals


def preset_category(presets_dir, name):
    vals = scene_values(presets_dir, name)
    if vals is None:
        return None
    if vals.get("view") == "butterfly":
        return "flowerchild"
    if vals.get("view") == "show":
        return "show"
    if vals.get("material") == "jelly":
        return "candy"
    return "lamp"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tsv", required=True)
    ap.add_argument("--presets", required=True)
    ap.add_argument("--binary", required=True)
    ap.add_argument("--base-seed", type=int, required=True)
    ap.add_argument("--slice", type=int, required=True)
    ap.add_argument(
        "--env-prefix", default="", help="space-separated KEY=VALUE runtime environment"
    )
    ap.add_argument("--lamp-pct", type=float, default=70)
    ap.add_argument("--show-pct", type=float, default=15)
    ap.add_argument(
        "--flowerchild-pct",
        "--butterfly-pct",
        dest="flowerchild_pct",
        type=float,
        default=15,
    )
    ap.add_argument("--candy-pct", type=float, default=0)
    args = ap.parse_args()

    opts = parse_tsv(args.tsv)
    s = args.slice
    preset_mode = s % 4 == 3
    preset_names = (
        sorted(f[:-5] for f in os.listdir(args.presets) if f.endswith(".conf"))
        if os.path.isdir(args.presets)
        else []
    )

    cols = {k: "-" for k in COLS}
    cols["slice"] = str(s)
    cols["mode"] = "preset" if preset_mode else "draw"
    env_assign = []

    rng = Rng(stream(args.base_seed, s, "category"))
    category = rng.weighted(
        [
            ("lamp", args.lamp_pct),
            ("show", args.show_pct),
            ("flowerchild", args.flowerchild_pct),
            ("candy", args.candy_pct),
        ]
    )
    cols["category"] = category

    # One distinct seed per (base_seed, slice): slices never collide, and a
    # replay of the same base seed reproduces the run. This is also the
    # pattern seed (pattern-seed=auto), logged next to it.
    seed = (stream(args.base_seed, s, "seed") % 4294967295) + 1
    cols["seed"] = str(seed)
    cols["pattern-seed"] = str(seed)
    env_assign.append(f"NCZ_MAGMASIMPLEX_SEED={seed}")
    env_assign.append("NCZ_MAGMASIMPLEX_PATTERN_SEED=auto")

    def setenv(name, val):
        cols[name] = val
        env_assign.append(f"{opts[name]['env']}={val}")

    if preset_mode:
        rng = Rng(stream(args.base_seed, s, "preset"))
        pool = [p for p in preset_names if preset_category(args.presets, p) == category]
        if pool:
            name = rng.pick(pool)
            cols["preset"] = name
            env_assign.append(f"NCZ_MAGMASIMPLEX_PRESET={name}")
        # The light shows carry no lamp knobs; a preset slice sets nothing else.
    else:
        concrete = [x for x in opts["palette"]["choices"] if x != "auto"]
        lamp_pal = [x for x in concrete if not x.startswith("show-")]
        show_pal = [x for x in concrete if x.startswith("show-")]
        rng = Rng(stream(args.base_seed, s, "palette"))
        if category == "flowerchild" and rng.maybe(0.7) and show_pal:
            setenv("palette", rng.pick(show_pal))
        elif rng.maybe(0.20):
            eras = [x for x in opts["palette-era"]["choices"] if x != "auto"]
            era = rng.pick(eras)
            setenv("palette", "auto")
            setenv("palette-era", era)
        else:
            setenv("palette", rng.pick(lamp_pal))

        rng = Rng(stream(args.base_seed, s, "world"))
        setenv("world", rng.pick([x for x in opts["world"]["choices"] if x != "auto"]))

        rng = Rng(stream(args.base_seed, s, "view"))
        if category == "lamp":
            setenv("view", rng.weighted([("inside", 45), ("field", 30), ("glass", 25)]))
        elif category == "show":
            setenv("view", "show")
        elif category == "flowerchild":
            setenv("view", "butterfly")
        else:
            setenv("view", rng.weighted([("field", 60), ("inside", 40)]))

        rng = Rng(stream(args.base_seed, s, "material"))
        if category == "candy":
            setenv("material", "jelly")
        else:
            setenv("material", rng.weighted([("milky", 65), ("translucent", 35)]))

        rng = Rng(stream(args.base_seed, s, "glitter"))
        if category == "lamp" and rng.maybe(0.15):
            setenv("glitter", rng.pick(["none", "silver", "gold", "rainbow"]))
        # otherwise glitter follows the palette (auto): the glitter lamps only

        rng = Rng(stream(args.base_seed, s, "colourway"))
        setenv("colourway", draw_int_mid(rng, opts["colourway"]))

        rng = Rng(stream(args.base_seed, s, "kaleidoscope"))
        if category == "show":
            setenv("kaleidoscope", str(rng.next() % 6 + 3) if rng.maybe(0.25) else "-1")
        elif category == "flowerchild":
            setenv("kaleidoscope", str(rng.next() % 5 + 4) if rng.maybe(0.2) else "0")
        else:
            setenv("kaleidoscope", "0")

        for name in FLOAT_OPTS:
            rng = Rng(stream(args.base_seed, s, name))
            setenv(name, draw_float(rng, name, opts[name]))

    env_assign.append("NCZ_MAGMASIMPLEX_PERF_LOG=1")
    cmd = (
        "env "
        + (args.env_prefix + " " if args.env_prefix else "")
        + " ".join(env_assign)
        + " "
        + args.binary
    )
    row = [cols[k] for k in COLS] + [cmd]
    assert all("\t" not in x for x in row), "tab in a slices.tsv field"
    sys.stdout.write("\t".join(row) + "\n")


if __name__ == "__main__":
    main()
