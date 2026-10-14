# Black Hole "Randomize everything"

Operator request (2026-10-05): *"with black hole can we have a 'randomize
everything' option which includes the scene type"*.

`blackhole_gles3` gains a `randomize` option (enum `off` | `look` |
`everything`, default `off`, group Look, simple/basic level, environment
`NCZ_BLACKHOLE_RANDOMIZE`, command line `--randomize=...`).

* **off** – the shipped behaviour; the user's settings are used as they are.
* **look** – at each start, draw a new palette and a new set of look/tuning
  values inside the safe envelope below.
* **everything** – as `look`, and also pick the scene type (the preset) at
  random from the presets this GPU class may run.

The roll is printed once per start:

```
[diag] blackhole randomize seed=<seed> mode=<look|everything> class=<weak|mid|strong> pool=<class|heavy> preset=<id|-> palette=<name> pinned=<names|-> rolled=<names|-> exposure=... ...
```

Set `Random seed` (`--seed=`, `NCZ_BLACKHOLE_SEED`, alias
`NCZ_BLACKHOLE_FIXED_SEED`) to reproduce a roll exactly: the same seed drives
both the roll and the per-launch shader randomness (the `seed` option), using
the named-stream hash that `xshadertoy` already uses for `iSeed`
(`xst_core.c`: `xst_stream(seed, "<name>")`), so each draw is keyed by option
name and adding options never shifts the others.

## When it re-rolls

`blackhole_gles3` has **no scene/preset transition**: a run repeats the same
scene (`scene-cycle`) and the same camera path (`duration`), and the palette
cycling is a separate, user-owned feature. Therefore, as the brief allows when
there is no scene transition, the scene type and the look are re-rolled **only
at each screensaver start** (one roll per process). The existing palette cycle
and per-flyby cycle are left untouched.

## Scene selection and GPU class

A preset is only picked when its declared `min-gpu-class` (the `[preset]`
section of `assets/screensaver-chooser/options/presets/blackhole/<id>.conf`,
the same value the catalogue shows) is at or below the class of the GPU that
will render it — `NCZ_GPU_CLASS`, set by the launcher from
`ncz-screensaver-calibrate`, or the renderer-string table as fallback.

A scene flagged above the class is rolled only when the user opted into the
**heavy pool**: the launcher's `pool-gpu-class` resolved to `all`/`strong`,
passed to the hack as `NCZ_GPU_POOL=strong`. `NCZ_GPU_POOL=mid` allows up to
mid, and the default (unset) never exceeds the current class.

If the catalogue holds no allowed preset the roll is refused and the engine
runs with the user's settings (one `[diag] ... roll skipped` line).

## Shipped default

The ncz-screensavers schema (`config/dev.ncz.screensaver.gschema.xml`) ships
Black Hole as the default screensaver: `mode='one'`,
`hack-id='blackhole_gles3'`, `blackhole-color-mode='cygnus'` and
`hack-options={'blackhole_gles3': {'randomize': 'everything'}}`. The Cygnus
palette is therefore pinned (the colour mode is not `stylized`) while the
scene type and the remaining look options roll. This default lives in its own
commit so it can be dropped or adjusted without touching the feature. The
cix-installer post-install hook seeds the same values only when the installed
engine's `--dump-schema` lists `randomize`.

## Pinning: a roll never overwrites what you set

A value is **pinned** when the user (or the shipped image default) set it
explicitly; a roll then leaves it alone. The precedence, highest first:

1. **environment / command line** — an `NCZ_BLACKHOLE_*` variable or a
   `blackhole_gles3 --<option>=…` flag. (The engine's own command line is
   honoured because the roll runs after these are known but never writes a
   value the user passed.)
2. **pinned stored values** — any option present in the per-saver
   `hack-options` (`dev.ncz.screensaver`, type `a{sa{ss}}`), plus the global
   `blackhole-color-mode` key whenever its value is not the literal vendor
   default `stylized` (this is what pins the shipped **Cygnus** default; the
   comparison is against `stylized`, not against the compiled schema default,
   so a Cygnus image default stays pinned).
3. **rolled** — the values this feature draws.
4. **engine default** — the schema default for anything still untouched.

The launcher computes the pinned set from the stored options, the legacy colour
mode and the ambient environment and passes it as `NCZ_HACK_PINNED`. The engine
also pins its own `--<option>=` flags. When no launcher is involved (a raw
headless run) an `NCZ_BLACKHOLE_*` already in the environment counts as pinned.
`--seed` / `NCZ_BLACKHOLE_SEED` reproduce a roll exactly, and `preset` can be
pinned so the scene is fixed while the look still rolls.

The `[diag]` line names the `pinned` and `rolled` sets, so it is always clear
which options the roll changed.

## The safe envelope

Every rolled value is inside the option's schema `[min,max]` **and** inside the
safety envelope from the black-floor audit
(`docs/audit/blackhole-black-floor-evidence/ROOTCAUSE.md`) and the weak-GPU
defaults in `docs/GPU-CLASSES.md` (render scale 0.5, no bloom, halved
density). The unit test pins this table (`tests/test_blackhole_randomize.c`)
and checks 30 000 seeded rolls.

| option | schema range | weak | mid | strong | why |
|---|---|---|---|---|---|
| `exposure` | 0.2–4 | 0.60–1.40 | 0.60–1.60 | 0.60–1.80 | lower bound keeps the frame off black, upper keeps the disk from clipping |
| `disk-brightness` | 0.2–3 | 0.70–1.40 | 0.70–1.50 | 0.70–1.60 | same, relative to the per-launch disk |
| `bloom` | 0–3 | 0.00 | 0.00–0.90 | 0.00–1.80 | the audit's rule: no bloom on weak; limited on mid |
| `fringe` | 0–1 | 0–0.35 | 0–0.35 | 0–0.35 | fringing adds high-frequency colour, not brightness |
| `beaming` | 0–2 | 0.50–1.60 | 0.50–1.60 | 0.50–1.60 | keeps both the bright and dim side visible |
| `star-density` | 0–3 | 0.40–1.00 | 0.50–1.40 | 0.50–1.80 | weak default is halved; cap keeps the star loop bounded |
| `nebula` | 0–2 | 0.00–0.60 | 0.20–1.20 | 0.20–1.50 | weak halves it, and it is a full-screen fill |
| `jet` | 0–2 | 0.40–1.50 | 0.40–1.50 | 0.40–1.50 | always-on jets (operator 2026-09-25) |
| `hot-sector` | 0–1 | 0.05–0.45 | 0.05–0.45 | 0.05–0.45 | stylized disk feature only |
| `temperature` | 0.3–1.6 | 0.70–1.30 | 0.70–1.30 | 0.70–1.30 | colour shift, harmless to brightness |
| `rotation-speed` | 0–3 | 0.40–1.60 | 0.40–1.60 | 0.40–1.60 | motion, no cost |
| `precession` | 0–4 | 0.30–1.80 | 0.30–1.80 | 0.30–1.80 | motion, no cost |
| `disk-outer` | 6–80 | 8–30 | 8–30 | 8–30 | larger disks cost fill-rate on weak |
| `jet-length` | 5–250 | 10–60 | 10–60 | 10–60 | long jets are mostly sky fill |
| `spin` | 0–0.998 | 0–0.90 | 0–0.90 | 0–0.90 | the analytic Kerr table is the cost |
| `ripples` | 0–2 | 0.40–1.50 | 0.40–1.50 | 0.40–1.50 | GW ripples, scene only |
| `speed` | 0.1–4 | 0.60–1.60 | 0.60–1.60 | 0.60–1.60 | time scale, no cost |
| `palette-transition` | 0–120 | 3–15 | 3–15 | 3–15 | cross-fade only |
| `palette` | 10 choices | any | any | any | all ten colour models are safe |

Deliberately **not** rolled, even under `everything`:

* **developer-only options** — the schema's Capture group (and any
  `level: advanced` capture settings). The roll never touches them, so it
  cannot turn on capture or a debug path; the Developer switch only gates the
  UI, not the roll.
* **style** (`classic`/`enhanced`) — moves a whole bundle; the preset's style
  is respected.
* **camera path** (`flyby`, `lens`, `distance-*`, `inclination`, `frame-*`) —
  scenes own their cameras, so rolling it would break the scene's framing.
* **system/safety** (`quality`, `scene-quality`, `adaptive`, `lens`,
  `render-scale`, `capture-*`, `scene-*`, `mass-ratio`, `separation`,
  `temperature` is rolled but not scene shape) — left to the launcher and the
  scene.

The all-black guarantee comes from the lower bounds: exposure ≥ 0.6 and
disk-brightness ≥ 0.7 with a disk always present, a positive jet, a minimum
star/nebula fill, and no weak bloom. The headless render smoke asserts a
non-black ratio and no GL error for each seed.

## Settings UI

In the Black Hole simple-mode settings: a **Randomize everything** switch
(writes `randomize=everything`/`off`) and a **Roll now** action (writes
`randomize=everything` plus a fresh seed, so the next start draws a new
configuration; keep it by turning Randomize off). Both are shown only when the
connected engine's schema exposes the `randomize` option, so older engines do
not show them. libsingularity widgets only (`SwitchRow`, `ActionRow`).

## Out of scope / open

* The launcher does not yet persist the *derived* rolled values into
  `hack-options`; `Roll now` sets the seed that determines them. Turning
  Randomize off therefore returns to the user's own values rather than keeping
  the exact roll. A future engine query (`--print-config` after a roll) could
  persist them if wanted.
* No scene/preset transition exists, so there is nothing to hook at a cycle
  boundary; this is stated above, as the brief requires.
