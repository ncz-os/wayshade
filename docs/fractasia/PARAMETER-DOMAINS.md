# Curated Parameter Domains (v1, 2026-09-30)

Every algorithm family in `fractasia_gles3` is parameterised by
a small set of valid ranges. The randomizer picks inside those ranges
from the seed; the user may pin any of them with `--family`,
`--type`, or `--iterations`. The numbers below are the curated,
taste-tested, GPU-safe envelopes.

The rule: every parameter must be picked from a closed interval, every
boundary is non-NaN / non-infinity safe inside the fragment loop, and
every type is reachable by at least one stable domain. Visual diversity
is measured by Hamming distance between rendered first frames at seed
delta = 1.

## F1 — Escape-time polynomial

**Algorithm**: `z ← z^n + c`, with `n ∈ {2, 3, 4, 5, 6, 7, 8}`, Mandelbrot
or Julia variant (c static or c = z on entry).

### z² (Mandelbrot / Julia / Julia variants)

- Bailout radius: 2.0 (Mandelbrot) or 4.0 (Julia).
- Iteration budget: 64 (low quality) / 128 (medium) / 256 (high) /
  512 (ultra, Mali-driver-rejected on older drivers).
- Maximum c radius for the Julia parameter sweep: 1.25 (root-2 inside
  the cardioid boundary, always connected).
- View extents:
  - Mandelbrot: x ∈ [-2.5, 1.0], y ∈ [-1.25, 1.25].
  - Julia: x ∈ [-1.6, 1.6], y ∈ [-1.6, 1.6] (default — the operator
    may zoom inside the connection domain).
- Slow parameter animation: c traces a cardioid at radius 0.7885 in
  the Julia case (same path the existing `hyprsaver_julia.frag`
  uses, but our deterministic seed chooses the phase offset).

### Multibrot (n = 3..8)

- Bailout radius: 1.5 (n=3) .. 1.0 (n=8). Higher n needs tighter
  bailout or the orbit diverges in pixel scale noise.
- Iteration budget: 48 (n=3) / 64 (n=4) / 96 (n=5..8) — higher n
  needs fewer iterations because the orbit exits faster.
- View extents: x ∈ [-1.5, 1.5], y ∈ [-1.5, 1.5] for Mandelbrot side;
  Julia side smaller.

### Burning Ship

- Standard escape-time, but with `abs()` on both `z.x` and `z.y`
  before squaring.
- Iteration budget: 96 (low) / 192 (high).
- View extents: x ∈ [-2.0, 1.0], y ∈ [-1.0, 1.0].

### Tricorn

- `z ← conj(z)² + c`. Same iteration budget as Mandelbrot z².
- View extents: x ∈ [-2.0, 1.0], y ∈ [-1.25, 1.25].

### Spider

- `z ← z² + c`, with `c` updated to `c_old + z / 2` per iteration.
- Iteration budget: 96 (low) / 192 (high).
- View extents: x ∈ [-2.0, 2.0], y ∈ [-1.5, 1.5].

### Phoenix

- Persistent c parameter: `z ← z² + c_old + c_new · z_old`,
  `c_new ← z_old · p`.
- Iteration budget: 128 (low) / 256 (high).
- p range: [0.5, 1.5].

### Magnet / Manning

- `z ← ((z² + c − 1) / (2z + c − 2))²`. Iteration budget: 96.
- View extents: x ∈ [-2.5, 2.5], y ∈ [-1.5, 1.5].

## F2 — Newton-Raphson family

- Iteration budget: 32 (low) / 64 (high).
- Root polynomial:
    - Cubic: `z³ − 1` → 3 roots at unit cube roots of unity.
    - Quintic: `z⁵ − 1` → 5 roots.
    - Quartic: `z⁴ − 1` → 4 roots.
    - Octic: `z⁸ − 1` → 8 roots (note: driving convergence can
      require Newton's method to be augmented with a small `μ`
      jitter — see PARAMETER-DOMAINS §F2).
- Step size clamp: `|Δz| ≤ 4.0` to prevent runaway divergence on
  boundary pixels.

## F3 — Quaternion Julia (4D escape-time)

- Quaternion multiplication `q1 · q2` with components `(x, y, z, w)`,
  escape when `|q|² > 4.0`.
- Iteration budget: 24 (low) / 48 (high). The 4D orbit exits faster
  than 2D, so we cap lower.
- Quaternion c components: each in [-1.0, 1.0], unit quaternion so
  `|c|² ≤ 1.0` (always connected in 4D).
- Ray direction: per-pixel deterministic 3D slice picked from seed;
  slices are not animated because the 4D figure cannot be watched in
  real time otherwise.

## F4 — Strange attractor basin colouring

- Algorithm: walk `N` steps of the attractor ODE starting from a
  per-pixel deterministic initial condition; the pixel colour is
  derived from the final position or the iteration at which the orbit
  leaves a unit box.

- **Lorenz (σ=10, ρ=28, β=8/3)**: dt = 0.005, 800 steps per pixel
  (no animation; the figure is too noisy to morph).
- **Rossler**: a = 0.2, b = 0.2, c = 5.7; dt = 0.05, 800 steps.
- **Henon (a=1.4, b=0.3)**: 100 steps per pixel.
- **Pickover (α=2.24, β=0.43, γ=0.79, δ=-0.65)**: 100 steps per pixel.
- **Popcorn (f, h, g parameters in [-0.5, 0.5])**: 30 steps per pixel.

All F4 families are rendered in "deterministic-per-pixel" mode — the
visual is the basin of attraction, not a moving trajectory.

## F5 — IFS / Plasma / L-system

- IFS: 4 affine maps, contraction ≤ 0.5, depth 5. Total transformed
  per pixel: ~256, with density accumulator.
- L-system: 2D turtle projection, depth 6, single axiom. Production
  rule is family-specific (Sierpinski, Koch snowflake, Dragon curve,
  Plant).
- Plasma: midpoint displacement with seed-driven initial corners;
  roughness parameter λ ∈ [0.4, 0.7] for fractalness.

## F6 — Lyapunov exponents

- 1D binary map `(a, b) → ax(1-x)`, `bx(1-x)`, alternating; sequence
  length 16 (from seed); exponent sign classifies the orbit.
- Parameter `a ∈ [3.4, 4.0]`, `b ∈ [3.4, 4.0]`.
- Pixel x = position in `[a, b]` parameter space; pixel y = random
  initial x.

## F7 — Cellular automaton (Wolfram)

- 1D Wolfram rule (30, 90, 110, 184), 200 ticks, buffer scrolls down
  the screen.
- Initial state: single 1 in the middle (Rule 30 default); for Rule
  110 the initial state is random from the seed.
- Cells are 1 pixel wide each; the figure scrolls at 30 cells/sec.

## Tunable user controls

The user-facing option set is the union of all-of-the-above envelope
exposed through the `--style`, `--family`, `--type`, `--iterations`,
`--speed`, `--quality`, `--seed`, `--exposure`, `--palette` flags.
The default for every flag is taken from the curated domain above;
the randomizer (when the seed is non-zero and `--family` is `random`)
picks a domain inside the curated ranges using a deterministic xorshift
RNG keyed on the seed.