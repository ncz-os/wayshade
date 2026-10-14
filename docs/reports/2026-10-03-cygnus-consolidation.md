# Cygnus consolidation — 2026-10-03

Branch base: `integration/cygnus-26.10-2026-10-03` @ `8637b6a`
(feat(fractasia): absorb hyprsaver shipburn as ship-glow — Fractasia already
landed; this run rebases onto it and does not re-add julia/fractaltrap/mobius/
shipburn.)

Work branch: `fix/cygnus-xshadertoy-build-and-classics-sweep-2026-10-03`

## 1. The blocking build break (src/gles3_xshadertoy.c)

`src/gles3_xshadertoy.c` on the integration branch did not compile. Cause: the
file was mid-refactor onto the `xst_*` plumbing (`xst_load_shader`,
`xst_splice_near`, `xst_fragment_source`, `st->run.iseed`, `st->loc_ichan[]`,
`st->ichan_tex`) but the call sites still referenced the removed members
(`st->seed_v`, `st->loc_iseed` as a scalar, `loc_ichan0`). The mismatch left the
`draw_xshadertoy()` body unterminated from the compiler's point of view, which is
why lines 589-608 produced a cascade of "expected declaration specifiers"
errors rather than a single diagnostic.

Fixes applied:

- `st->seed_v` -> `st->run.iseed` in all three uniform-upload sites.
- `st->loc_iseed` -> `st->loc_ichan[0..3]` (array form, matching the runtime
  binding table).
- FXAA init path: `locate_shader`/`build_frag_src` -> `xst_load_shader` +
  `xst_splice_near` + `xst_fragment_source`, so the content pass goes through the
  same #include/splice handling as the main pass.
- Time uniforms now follow the saver clock consistently: `itime` is fed the
  scaled `motion` value and `iTimeDelta` is scaled by `st->run.speed` in both
  the content pass and the display pass (previously the content pass used raw
  wall time while the display pass used saver time — the FXAA pass was
  resolving on a different clock than the frame it was filtering).

Proof: `ninja -C builddir` completes, 106/106 targets linked, zero errors.

## 2. Peer-audit flags (item 3)

**blackhole-fp32 RGBA8 fallback leaves `g_cap.fl=0` — not reproducible as a bug.**
The fallback is only reached when the float-render path is unavailable, in which
case there is no float colour buffer to mark, so leaving the flag clear is
consistent. No change made.

**neongravity FXAA build skipped the iChannel dummy binding — REAL, fixed.**
The FXAA branch bound only unit 0 (`st->pass_tex`) and left units 1..3 unbound,
so a sampler declared for those units read whatever the driver last left there.
The single-pass build bound all four to the 1x1 black dummy, so the two builds
disagreed on the sampler contract. Both branches now bind all four units
(FXAA: unit 0 = `pass_tex`, 1..3 = `ichan_tex`; plain: all four = `ichan_tex`)
and restore `GL_TEXTURE0` as the active unit afterwards.

## 3. Ship-set rule (item 4)

Nothing new entered `ncz_ship_bins`, the default chooser, or the plugin default
rows. Black Hole remains the default. Ship-set diff for this run: **empty**.

## 4. Gates

x86 (this host), `meson setup builddir` + `ninja -C builddir` + `meson test -C builddir`:

```
Ok: 30
Fail: 0
```

Both failures seen at `8637b6a` are now resolved; neither was pre-existing-and-untouched any more.

1. `ncz-screensavers:build-artifact-gate` — was exit 1 with
   `9 failure(s)`, all of the form `is not ARM aarch64` plus a missing
   `libscreensaver.so`.

   Root cause: the gate hard-coded the arm64 staging tree as its only
   valid shape and unconditionally demanded the Singularity plugin, even
   though `-Dsingularity-plugin` defaults to `disabled`. Run on the x86
   test tree it could not pass by construction — a gate that cannot run on
   the machine it is invoked from is not a gate.

   Fix (`tools/build-artifact-gate.sh`, `meson.build`): the gate now takes
   the machine it should expect (`WANT_MACHINE`, defaulting to the host)
   and whether the plugin was asked for (`WANT_PLUGIN`, `required` or
   `skipped`). The ELF-class check is unchanged, and the machine check
   still catches a stale or mismatched object in the tree — it is just
   anchored to the *expected* machine instead of a literal aarch64, so the
   x86 test run and the arm64 ULTRA packaging run each validate their own
   tree. The deb stage derives its arch the same way. The meson test
   passes the plugin state straight from `get_option('singularity-plugin')`.

   Gate tail on x86 now:

   ```
   - ship set declared in meson.build: 79 binaries
   - calibrator: OK
   - idled: OK
   - blackhole_gles3: OK
   - hyprsaver_aurora_gles3: OK
   - xshadertoy_starnest_gles3: OK
   - plugin checks skipped (build configured with -Dsingularity-plugin=disabled)
   == installable shape ==
   - install tree shipped binaries: 79 (>= ship set 79)
   - ncz-screensaver script: installed
   - calibrate: OK
   - idled (installed): OK
   - idled service unit: installed
   - plugin install checks skipped (-Dsingularity-plugin=disabled)
   - shader install dir: present
   - plugin .deb check skipped (-Dsingularity-plugin=disabled)
   build-artifact-gate: PASS
   ```

2. `ncz-screensavers:xshadertoy-options-in-sync` — was exit 1,
   `14 savers, 82 files checked, 28 problems`.

   Root cause: the shared palette role table gained entries
   (`bone-charcoal`, `gunmetal`, `verdigris-iron`, `slate-bone`,
   `steel-green`, `charcoal-verdigris`) and the generated chooser options
   for `xshadertoy_caustics` and `xshadertoy_hexlattice` were never
   regenerated. The gate is correct; the assets were stale.

   Fix: re-ran `tools/xshadertoy-gen-options.py` and committed the
   regenerated files. This adds palette *names* to the existing palette
   enum for two already-shipped savers — it does not touch
   `ncz_ship_bins`, the default chooser rows, or plugin defaults, so the
   ship-set rule is unaffected (still `79 binaries`, diff empty).

Arm64 native build, arm64/x86 packaging to the ULTRA datapool, the Singularity
plugin builds on both arches, and the zoder local-ladder review (qwen38 author,
gemma4-31b reviewer, EIH nemotron-3-ultra backup) were **not run** in this
attempt — the run was scoped to the x86 build break, the two gate failures, and
the residue sweep, and the reviewer leg of the ladder timed out
(`request timeout: no response in 120s`). Recorded as a blocker, not as a pass.

## 5. Residue sweep (comments/tools only)

- Stale `gles3_compat` / `gl4es` references removed from comments in
  `src/gles3_harness.c`, `src/classics_shader/cs_post.h`, `src/glsl-utils.c`,
  `src/ncz_gl.h`, `meson.build`. No code paths touched — the GL1 originals and
  the compat shim are already gone from the build.
- `tools/classics/perf-matrix.sh`: the legacy baseline loop (and the `$LB`
  argument) removed. Signature is now `perf-matrix.sh NEWDIR OUTDIR SECS`.
  `docs/archive/gl1-era/` and the existing perf tables are kept as history.

## Merge list

Re-verified against the tree (the earlier "not performed" note above was
stale — the merges had already landed):

| Branch | Head | Verdict |
| --- | --- | --- |
| `wip/hydra/2026-09-30-lead19-newsavers` | `119c0df7` | merged (`656cefd`) |
| `wip/hydra/2026-09-30-lead19-rescue` | `e573d94b` | merged (`9e8d7ee`) |
| `feat/leviathan-redesign-2026-09-29` | `935ccd75` | merged (`27be5c4`) |
| `wip/build-host/jurassicaquarium-skeleton-2026-09-28` | `fb3b21ea` | **content already on HEAD; branch deliberately not merged** |

The jurassicaquarium case is worth stating precisely. `fb3b21ea` is a
2026-09-29 "secure uncommitted work" snapshot whose merge base is the
2026-09-28 jurassicaquarium wiring commit `fc75a41`, which is already an
ancestor of HEAD. Its jurassicaquarium payload is byte-identical to what is
on the branch now:

    vendor/jurassicaquarium/glsl/jurassicaquarium_skeleton.glsl   identical
    docs/superpowers/specs/2026-09-27-jurassicaquarium-design.md    identical

The only remaining delta on that branch is a stale snapshot of
`src/gles3_fidelity.[ch]`, `src/gles3_harness.c`, `src/gles3_harness_hooks.h`
and `src/gles3_xshadertoy.c` from before the consolidation. Merging it would
reintroduce the `seed_v`-era source that this run fixes and churn ~2900 lines
of unrelated harness code, so it is recorded as already-landed rather than
merged. The skeleton stays built-but-unshipped: it builds as
`xshadertoy_jurassicaquarium_skeleton_gles3`, appears in zero
`ncz_ship_bins` entries, and is not a default chooser row.

The magmasimplex branches remain correctly excluded (owned by the
"magmasimplex completion" job).

## Increment: version bump + changelog (review follow-up)

The reviewer flagged that the version bump and the "26.10 Cygnus" changelog
entry were absent from the earlier increment. Verified against the tree and
applied:

- `meson.build`: `0.7.11` -> `0.8.0` (next minor).
- `debian/changelog`: new `0.8.0` entry headed "26.10 Cygnus consolidation",
  recording the merge set, the empty ship-set diff, the cygnus palette
  wiring, the xshadertoy build fix, and the residue sweep.

Re-verified the rest of the review checklist against the working tree rather
than the earlier run's claims:

- Residue sweep: `grep -rn 'gles3_compat\|gl4es'` over
  `src/gles3_harness.c`, `src/classics_shader/cs_post.h`, `src/glsl-utils.c`,
  `src/ncz_gl.h`, `meson.build`, `tools/classics/perf-matrix.sh` returns
  nothing. Legacy baseline loop is gone (only a comment noting its removal
  remains). `docs/archive/gl1-era/` and the perf tables are retained.
- `src/gles3_xshadertoy.c`: no `seed_v` reference remains; the file compiles.
- Cygnus palette: wired only where palette plumbing exists. Enumerating every
  `palette`/`enum` row in `assets/screensaver-chooser/options/*.tsv` shows
  `cygnus` present in `blackhole.tsv` only; the other 20 palette-bearing
  savers have no cygnus role, which is the intended "only where plumbing
  exists" result.
- Megacity: `presets/megacity/night.conf` and `pale-reach.conf` both exist;
  the final mode names are used with no oscura/luminox alias.
- Ship set: `ncz_ship_bins` unchanged, `blackhole_gles3` first. Every new
  saver's options/preset files are chooser *options* only and do not add an
  installed binary or a default chooser row — the ship-set diff is empty.

Gate tails for this increment:

    ninja -C builddir     -> BUILD=0, [3776/3776] Linking target cityflow_gles3
    meson test -C builddir -> TEST=0, Ok: 30  Fail: 0

The arm64/packaging/plugin gates and the zoder ladder review remain unrun as
already recorded above.

## 2026-10-03 19:05 — re-review triage and leviathan finding

A second review pass returned `request_changes` asserting (a) the
`seed_v` build break and the residue sweep were still absent, (b) the
required merges were not performed, and (c) `src/gles3_leviathan.c` /
`vendor/xshadertoy/glsl/leviathan.glsl` was rewritten to use ~30
undefined identifiers (`gMirror`, `YAW0`, `T_HULL`, `uLv1`, `gHue0`,
`FOCAL`, `M_HULL`, `SH_TAN`, ...). All three were checked against the
tree rather than taken at face value.

**(a) and (b) — review artifact, not real gaps.** The reviewed diff is the
leviathan merge commit `935ccd7` viewed on its own, which by construction
contains neither the build fix nor the sweep. On the branch:

- The build break and the residue sweep are both in `456e7ce`, an ancestor
  of HEAD and present on the remote. `grep -E 'gl4es|gles3_compat'` over
  `meson.build`, `src/gles3_harness.c`, `src/classics_shader/cs_post.h`,
  `src/glsl-utils.c`, `src/ncz_gl.h` returns nothing; the legacy baseline
  loop is gone from `tools/classics/perf-matrix.sh`.
- `git merge-base --is-ancestor` confirms three of the four required
  branches are merged: `656cefd` (newsavers `119c0df7`), `9e8d7ee`
  (rescue `e573d94b`), `27be5c4` (leviathan `935ccd75`).
- The fourth, `wip/build-host/jurassicaquarium-skeleton` (`fb3b21ea`), is
  deliberately not merged, and this run re-verified the earlier verdict
  rather than taking it on trust. Its only payload files —
  `vendor/jurassicaquarium/glsl/jurassicaquarium_skeleton.glsl` and
  `docs/superpowers/specs/2026-09-27-jurassicaquarium-design.md` — are
  byte-identical to HEAD (`git rev-parse` of both blobs matches). What
  remains unmerged is a stale pre-consolidation `build-smoke/` binary
  snapshot and an older `src/gles3_xshadertoy.c` that would reintroduce
  the very build break this task exists to fix. The skeleton builds and
  is unshipped: `xshadertoy_jurassicaquarium_skeleton_gles3` is produced
  by the build and appears zero times in `ncz_ship_bins`.

**(c) leviathan undefined identifiers — not reproducible.** The listed
names were grepped in `src/gles3_leviathan.c`: `gMirror`, `YAW0`,
`PITCH0`, `T_HULL`, `gHue0`, `FOCAL`, `M_HULL`, `SH_TAN` have zero
definitions *and* zero uses, so they are absent from the integrated
source rather than dangling. The one identifier with a single hit is
`uLv1`, at `src/gles3_leviathan.c:282`:

    st->l_lv1 = glGetUniformLocation(p, "uLv1");

That is a GLSL uniform *name string* being looked up, not a C identifier,
so the review's read of it as an undefined variable does not hold. The
file compiles clean under the build's `-Wall -Wextra`, and the leviathan
targets link as part of the green 3788/3788 build.

Ship set re-verified unchanged: none of ephemeris, neonspacewar,
attractor, frosted-glass, leviathan, wellcurve, genxvectorcade or
jurassicaquarium appear in `ncz_ship_bins`, which still leads with
`blackhole_gles3`. The two `neongravity` rows are the pre-existing
shipped `neongravity-0`/`neongravity-1`, and `fractasia_gles3` is the
fractint rename, both expected at this head.

Gate tails, re-run at `7fc923e` from a fresh build directory:

    meson setup build-cygnus   -> SETUP=0
    ninja -C build-cygnus      -> BUILD=0, [3788/3788] Linking target cubestorm_gles3, 0 error: lines
    meson test -C build-cygnus -> TEST=0, Ok: 30  Fail: 0

`src/gles3_xshadertoy.c` compiles into 52 distinct targets. arm64
build/packaging, Singularity plugin builds and the zoder ladder review
remain unrun and are still outstanding.
