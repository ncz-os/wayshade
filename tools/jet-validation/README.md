# jet-validation

`jet_outward_proof.py` — offscreen EGL/GLES3 capture that renders the **real**
`vendor/blackhole/blackhole.frag` and measures, per jet pole, whether the knot
structure drifts outward or inward over time.

It exists because the source-level test can only read the phase expression; this
tool proves it on a real GL context.

## How it works

1. Reads `blackhole.frag`, renames the single `void main(` to `bh_scene_main(`,
   and appends a diagnostic `main()` that calls the **unmodified** `jet_glow()`
   from the same file. No jet maths is reimplemented in Python.
2. Creates a surfaceless EGL context (`libEGL`/`libGLESv2` via `ctypes`) and an
   offscreen RGBA8 framebuffer, 256×256.
3. For each axis — `world_up = (0,1,0)` and `tilted = normalize(0.6,0.8,0)` —
   sets a side-on camera perpendicular to the axis, freezes the axis with
   `u_enh = 0`, and renders two frames at a fixed seed: `t = 0` and `t = 2`.
4. Writes both frames as PPM and cross-correlates the per-pole brightness
   profile against unsigned height `h`. A **positive** shift means the knots
   moved toward larger `h`, i.e. outward.
5. Writes `report.json` with the GL strings, the shader SHA-256, every uniform
   value, and the measured per-pole shift.

Fails closed: shader compile or link failure aborts with the info log.

## Run

Scratch dirs live on Projects disk, never in `/tmp` or a stage/tmp dir. Define
once per shell:

```sh
SCRATCH="${PROJECTS_SCRATCH:-$HOME/Projects/.scratch}"
mkdir -p "$SCRATCH/proj"
```

```sh
tools/jet-validation/jet_outward_proof.py --out-dir "$SCRATCH/proj/jetproof" --expect-outward
```

Negative control (pre-fix shader, must report the negative pole drifting
*inward*):

```sh
git show 03fd4d49:vendor/blackhole/blackhole.frag > "$SCRATCH/proj/old.frag"
tools/jet-validation/jet_outward_proof.py --shader "$SCRATCH/proj/old.frag" \
    --out-dir "$SCRATCH/proj/jetproof-old"
```

Expected knot drift is `1.6 / 0.55` world units per second on both poles
(+5.818 world units over the 2 s gap); the tool reports this as
`expected_shift_world_units` so the measurement can be checked against it.

The source-level companion is `tools/tests/test_blackhole_jet_outward.py` and
the tool's own checks are `tools/tests/test_jet_validation_tool.py`.