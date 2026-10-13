# Shader ledger — what our work replaced

This is the supersession record for ncz-screensavers 0.8.9: for every shipped
home-grown or superseding shader, the original it displaced or absorbed, with
the commit evidence and the licence obligation the original carries. The
per-item origin/licence table is `docs/PROVENANCE.tsv`; the licence texts are
in `NOTICES.md`. Verified from the file headers and the git history, not from
memory.

## Shipped supersessions

| our work (shipped) | original it replaced | kind | evidence | obligation retained |
|---|---|---|---|---|
| `fractasia_gles3` family `ship-glow` (`vendor/fractasia/glsl/ship_glow.frag`) | `hyprsaver_shipburn` (`vendor/hyprsaver/shaders/shipburn.frag`) | absorbed-into:fractasia | commit `8637b6a` "absorb hyprsaver shipburn as ship-glow and retire the standalone saver" | MIT, © 2026 Mara Vexa — attribution in `NOTICES.md` §3.1 and in the `ship_glow.frag` header |
| `fractasia_gles3` family `julia-glow` (`julia_glow.frag`) | `hyprsaver_julia` (`julia.frag`) | absorbed-into:fractasia | commit `4274b8f` "three absorbed families" | MIT, © 2026 Mara Vexa — header attribution present |
| `fractasia_gles3` family `orbit-trap` (`orbit_trap.frag`) | `hyprsaver_fractaltrap` (`fractaltrap.frag`) | absorbed-into:fractasia | commit `4274b8f` | MIT, © 2026 Mara Vexa — header attribution present |
| `fractasia_gles3` family `mobius` (`mobius.frag`) | `hyprsaver_mobius` (`mobius.frag`) | absorbed-into:fractasia | commit `4274b8f` | MIT, © 2026 Mara Vexa — header attribution present |
| `xshadertoy_neonhorizon_gles3` (in-house) | `hyprsaver_gridwave` (`gridwave.frag`) | displaced-by:neonhorizon | commit `2c367d0`; `debian/changelog` 0.8.2 "displaced by the in-house neonhorizon look" | MIT, © 2026 Mara Vexa (gridwave stays vendored, not shipped); prior art *Synthwave City* © Jan Mróz is CC-BY-3.0, credited in the neonhorizon header |
| `xshadertoy_neongravity_gles3` (in-house) | `xshadertoy_neongravity-1` (mrange CC0 port) and the earlier `xshadertoy_universeball` / `gravitywell` cuts | displaced-by:neongravity | commit subject `wip(shaders): preserve new visual prototypes` (shader/options/presets); wired into `ncz_ship_bins` 2026-10-05, replacing the third-party port | CC0-1.0 (the mrange port stays vendored, not shipped); MIT (universeball, Matt Vianueva) and the xscreensaver notice (gravitywell, Jamie Zawinski) are retained because the displaced originals stay in the tree |

## The neongravity supersession, landed 2026-10-05

The 0.8.2 changelog said `xshadertoy_universeball` and `gravitywell` were
"displaced by the in-house neongravity scene". The 0.8.9 changelog corrected
that because the shipped binary was still the third-party port. That is now
true:

- `vendor/xshadertoy/glsl/neongravity.glsl` is an in-house original whose header
  says it "replaces neongravity-0 and neongravity-1 (both ports of a third-party
  raymarcher; this is original work)". It was added in commit subject
  `wip(shaders): preserve new visual prototypes` with its options and presets.
- It is now wired into `ncz_ship_bins` as `xshadertoy_neongravity_gles3`,
  replacing `xshadertoy_neongravity-1_gles3` (so the ship count is unchanged at
  74). It renders an analytic per-pixel top view of infall: log-polar neon
  rings and spokes with Keplerian infalling bodies and a dark horizon disc. It
  has no raymarch and no third-party snippet inside.
- `xshadertoy_neongravity-1_gles3` (the mrange CC0 port of *Abstract
  gravitational well II*) is `displaced-by:xshadertoy_neongravity`. It stays in
  tree and still builds as a developer-only target; the 0.8.9 licence cleanup of
  its tonemapper still applies to that copy.
- `universeball` (MIT, Matt Vianueva) and `gravitywell` (xscreensaver port,
  Jamie Zawinski) are now recorded as `displaced-by:xshadertoy_neongravity` in
  `docs/PROVENANCE.tsv`: displaced by the in-house scene that actually ships.

The shas `50099b5` and `e17cf32` quoted in earlier reports no longer exist: the
repository history was rewritten on 2026-10-05 to remove a leaked credential,
so this ledger refers to branch names and commit subjects, not old shas.


## Dropped without absorption

| item | status | evidence | note |
|---|---|---|---|
| `xshadertoy_neongravity-0` | dropped-duplicate | `2c367d0`; changelog 0.8.2 | FXAA-only pass over `neongravity-1`; identical frames. Both third-party passes are now also superseded by the in-house `xshadertoy_neongravity_gles3` |
| `xshadertoy_iridescence` | unknown | `2c367d0`; changelog 0.8.2 "dropped from the cohort" | in-house clean-room original; no reason stated beyond the cohort cut. `7224ae1` (blur/fidelity audit) found its softness to be design intent, not a defect |
| `xshadertoy_stratus` | unknown | `2c367d0`; changelog 0.8.2 | in-house clean-room original; same as above |
| `selfreflect`, `stardome`, `stripeytorus` | unknown | `3549bab`, `39c30fd`, `7224ae1` | moved out of the main package to `ncz-screensavers-extra` because they declare CC0 but embed `License: Unknown` snippets (`stardome` also a CC BY-NC-SA 3.0 snippet). This is a licence cut, not a quality one; it does not fit the quality/duplication vocabulary, so the status is `unknown` with the licence reason recorded |
| vendored extras `attractor`, `aurora`, `caustics`, `caustictidepool`, `cellmosaic`, `chitin`, `hexlattice`, `marble`, `physarum`, `ridgeline`, `wellcurve` | unknown | `7224ae1` marks the set "opt-in not-yet-evaluated-for-shipping" | never in `ncz_ship_bins`; `ridgeline` is deliberately a calibration/stress shader |

## Provenance of our own fractal work (licence-relevant)

- `fractasia_gles3` does **not** derive from Fractint source code. The driver
  header and `docs/fractasia/LICENSE-ANALYSIS.md` state that only Fractint's
  public type-name/callback metadata is used; all shader maths is textbook
  implementation. Fractint's non-commercial/asset restrictions therefore do not
  attach to our code.
- Four of the seventeen `vendor/fractasia/glsl/*.frag` kernels **are** adapted
  from hyprsaver (MIT) and carry the MIT attribution obligation (table above).
- The 11 classics derive from xscreensaver: their original permission notice is
  preserved in every `src/classics_shader/cs_*.c` header and in the three
  upstream GLSL `src/*.c` files. No classic uses a CC-BY-NC term.
