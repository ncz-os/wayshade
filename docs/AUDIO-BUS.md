# AUDIO-BUS: the shared beat / audio layer

**Introduced:** 2026-10-05, magmasimplex "live" dynamics.
**Files:** `src/ncz_audio_bus.h/.c` (engine), `src/magmasimplex_beat.h/.c`
(adapter), `tests/test_ncz_audio_bus.c`, `tests/test_magmasimplex_beat.c`.

The beat layer is deliberately split in two:

* **`ncz_audio_bus`** — a saver-agnostic engine. It knows nothing about any
  saver, carries no GL and no magmasimplex names. It produces a plain
  `NczAudioSignals` struct once per frame.
* **`magmasimplex_beat`** — a thin adapter. It reads `NczAudioSignals` and maps
  them, per view mode, into the bounded drives the goo shader reads.

Any screensaver can adopt the engine by linking `src/ncz_audio_bus.c`, reading
one struct per frame and (optionally) writing its own adapter.

## What the engine does

1. **A deterministic virtual beat clock** seeded by the run seed. BPM is drawn
   in 96..132 with slow drift (≤ 2.5 BPM) and swing; 4/4 with three pattern
   variations and a half-bar breakdown every 16..31 bars. It emits kick/snare
   impulses with attack/decay envelopes. Same seed ⇒ same timeline.
2. **Optional real audio** (`opts.audio`: `off`/`system`/`mic`). Raw mono PCM
   at 22050 Hz is read through a child process (`parec`/`pw-record`), so **no
   link-time dependency is added**. A 1024-point radix-2 FFT (own
   implementation), a 256-sample hop, bass/mid/treble band energies with AGC,
   spectral-flux onset detection with an adaptive threshold, a tempo estimate
   from the onset envelope, and phase-locking of the virtual clock. If capture
   fails or is silent for several seconds it falls back to the virtual beat
   with **one** log line.
3. **A photosensitive-safety limiter**, always on regardless of options. The
   limited drive cannot change by more than ~25 % of its previous level per
   beat, at most 3 flash-like rises are allowed per second, and a red-heavy
   source is damped to 60 %. A beat is a smooth envelope, never a strobe.

Capture is build-gated by `NCZ_AUDIO_CAPTURE` (meson option `magma-audio`,
default on). With the option off the saver still builds and runs on the virtual
beat alone.

## Privacy

`audio=mic` listens to the default microphone **only when explicitly
selected**; it is never a default and never drawn from the seed. Every time
capture starts, a line is printed:

```
[diag] ncz-audio: capture started (mic via pw-record)
```

`system` listens to the default output monitor (what is playing), not the mic.
`off` (the default everywhere) does no capture at all.

## Adopting it in another saver

1. Add `src/ncz_audio_bus.c` to the executable and
   `#include "ncz_audio_bus.h"`.
2. Create once: `NczAudioBus *bus = ncz_audio_bus_create(seed, &opts);`
   with `opts = { dynamics, tempo, audio }` from your option layer.
3. Call `ncz_audio_bus_update(bus, dt)` once per frame (`dt` in seconds).
4. Read `const NczAudioSignals *s = ncz_audio_bus_signals(bus);` and drive the
   picture from `s->bass/mid/treble/onset_env/beat_phase/bpm/kick/snare/...`.
5. `ncz_audio_bus_destroy(bus)` at teardown.

`dynamics == 0` must leave the frame unchanged: multiply every term you add by
the drive (which is 0 when dynamics is 0), so the arithmetic is exactly the
pre-beat arithmetic. Do **not** apply the drives through a fixed offset.

The limiter is already applied inside the engine to the `kick`, `snare` and
`drive` signals it hands you; if you derive your own brightness signal from
`bass`/`mid`, run it through `ncz_beat_limiter_apply()` with a monotonic clock
before using it, so the photosensitive bounds still hold.

## magmasimplex per-mode mapping

The adapter (`magmasimplex_beat.c`) maps the generic signals to bounded drives.
All drives are multiplied by `dynamics`, so `dynamics=0` gives exactly 0.

| drive | bounds | lava (inside/field) | lava glass | show | butterfly | shader use |
|---|---|---|---|---|---|---|
| `squeeze` | 0..1 | 0.6·beat+0.3·onset+0.1·bass | gentler | beat | 0.6·beat+0.4·onset | radial squeeze ripple from the dish centre |
| `breath` | 0..1 | 0.45+0.45·bass+0.1·mid | 0.5+0.35·bass | 0.4+0.5·bass+0.1·mid | 0.35+0.45·bass+0.2·mid | saturation / brightness breathing |
| `burst` | 0..1 | 0.5·snare+0.3·onset | 0.35·snare+0.25·onset | max(snare,onset) | 0.7·snare+0.3·onset | droplet scale / glow pulse |
| `glow` | 0..1 | 0.6·beat+0.4·bass | 0.7·beat+0.3·bass | 0.5·beat | 0.55·beat+0.2·bass | emitter / onset glow |
| `flow` | 0..1 | 0.6·beat+0.4·onset | 0.3·beat+0.2·onset | 0.3·beat | 0.4·beat+0.2·onset | sim-time flow-velocity burst (C side) |
| `bloom` | 0..1 | mid | 0.7·mid | mid | 0.8·mid | mid-band light bloom |
| `radius` | −0.04..0.04 | 0.04·(2·beat−1) | … | … | … | dish-radius pulse (bounded ±4 %) |

World nudges stay inside the same bounds: `radial` scales `squeeze` by 1.2,
`dish` scales `flow` by 0.7, `drift` by 0.9.

The shader hooks are additive and every term is 0 when `u_dyn=0`, so the
pre-beat frame is reproduced byte-for-byte.

## Tests

* `tests/test_ncz_audio_bus.c` — determinism, BPM range/drift, onset detector
  on synthetic kicks (within 40 ms, zero false positives on noise), AGC, the
  limiter at 180 BPM full dynamics, dt clamping.
* `tests/test_magmasimplex_beat.c` — the per-mode mapping is bounded for any
  input (20 000 draws), `dynamics=0` gives exactly zero drives, determinism.
