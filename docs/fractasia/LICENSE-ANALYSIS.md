# License Analysis (v1, 2026-09-30)

This is the legal walk-through for the `fractasia_gles3` work.
The conclusion up front: we use the Fractint documentation and the
publicly published *type-name* and callback-registration metadata for the
source catalogue; we do NOT copy or adapt any
Fractint source code, formulas as written in their .frm files, image
assets, or any other restricted material. All shader math is
written from scratch using standard textbook treatments of the
algorithms.

## The Fractint licensing terms

The Fractint source distribution ships `fractsrc.txt` (or equivalent
in modern xfractint) with the following terms (paraphrased; the exact
wording lives upstream):

1. **Non-commercial use only.** Fractint is freely available for
   non-commercial use. Commercial use is prohibited.
2. **Source redistribution restrictions.** If you redistribute
   modified source code, the modifications must be made available
   (so the F1 community can re-base on them), but you may NOT
   redistribute the original Fractint source as a whole or as a
   substantially similar package.
3. **Asset restrictions.** The default `*.map`, `*.par`, and
   `*.ifs` files shipped with Fractint are explicitly *non-redistributable*
   by default; they are user-edited creations of the Fractint community
   and ship "as-is" without redistribution rights.

## What we are using

- The Fractint **type-name list and callback-registration metadata**
  (the names a user types at the Fractint `type=` prompt, the
  registration table in `common/fractalp.c`, and the per-type help files
  in `FracHelp/`). Type names are facts about the system, not creative
  expression; using them in our `--family`/`--type` enum is permissible.
- The Fractint **algorithm descriptions** in the user manual and on the
  public Fractint website. Algorithm descriptions are facts about the
  mathematics; using them to design our own shader implementations is
  permissible and is exactly what every GPU fractal renderer does.
- **Public-domain textbooks** for the underlying mathematics: Milnor's
  *Dynamics in One Complex Variable* (introductory material), the
  ChaosBook by Cvitanović et al., and the Wolfram MathWorld entries on
  Mandelbrot set, Newton fractal, Lyapunov exponent, IFS, and the
  Lorenz attractor. None of these are restricted by Fractint's
  license and none are needed to satisfy attribution for our work —
  we cite them in `docs/fractasia/REFERENCES.md` because that is
  scholarly practice.

## What we are NOT using

- The original **C source code** of `fractint.c`, `frm*.c`,
  `*.frm` formula files, or any other `*.c` / `*.h` from the
  Fractint distribution.
- The original **`.ifs`**, **`.map`**, or **`.par`** asset files.
- The original **CMAP colour maps** (these are restricted assets).
- Any **image, documentation screenshot, or other creative asset**
  from the Fractint distribution or website.

## What the shader code does

The fragment shader bodies in `vendor/fractasia/*.frag` are
written from scratch using the algorithm descriptions from public
textbooks. Every iteration loop is the textbook formulation:

- Escape-time polynomial: `z ← z^n + c`, with a small `n` (2..8) and
  c from the curated domain. This is the Mandelbrot / Julia / Multibrot
  family and the formula is published in every introductory
  complex-dynamics textbook.
- Newton-Raphson: `z ← z − f(z)/f'(z)` with `f(z) = z^k − 1`. The
  Newton fractal is a standard textbook topic.
- Quaternion escape-time: 4D multiplication is the standard Hamilton
  product. The "skin" of the Julia set in 4D is the union of the
  exit basins over a 3D slice — this is a published standard topic.
- Strange attractors: the Lorenz, Rossler, Henon, and Pickover
  equations are textbook ODEs. The "basin colouring" technique we
  use (paint a pixel by walking the ODE from a per-pixel
  deterministic initial condition) is a textbook technique for
  rendering attractor basins.
- IFS: affine maps `(x, y) → (a·x + b·y + e, c·x + d·y + f)` with
  contraction ≤ 0.5. Standard Barnsley IFS technique.
- L-system: turtle rewriting, depth ≤ 6. Standard L-system technique.
- Plasma: midpoint displacement. Standard technique from the
  1980s graphics literature.
- Lyapunov: the standard 1D logistic map alternating-parameter
  exponent formula. Standard textbook formula.
- Wolfram: 1D elementary cellular automaton. Standard Wolfram
  technique.

## Attribution

We do not claim any of the algorithm implementations as originally
ours. Every algorithm implemented here is a textbook algorithm. The
*novelty* of `fractasia_gles3` is the GPU-fragment-shader
encoding of all seven families in one binary with deterministic
selection between them — that is engineering, not mathematics.

Fractint (the program) is credited as the catalogue source for the
public type names and callback metadata used in the source inventory.
We do not claim any compatibility or
shared code with the original Fractint program.

The text of this document is original to ncz-screensavers and is
released under Apache-2.0 along with the rest of the project.
