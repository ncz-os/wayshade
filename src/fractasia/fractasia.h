/* SPDX-License-Identifier: Apache-2.0 */
/* fractasia.h — shared declarations for the Fractasia driver
 *                     and its smoke tester.
 *
 * The driver compiles one fragment shader per algorithm family at init
 * time. The shader sources are in vendor/fractasia/glsl/<family>.frag
 * (text original to ncz-screensavers). They depend on a preamble
 * injected by the driver (see fract_frag_preamble in
 * gles3_fractasia.c) which provides:
 *
 *     uniform float u_time;       seconds since init
 *     uniform vec3  u_resolution; viewport size in pixels (x, y, 1)
 *     uniform int   u_frame;      frame counter
 *     uniform vec4  u_seed;       four random floats in [0,1), constant per family cycle
 *     uniform int   u_iterations; hard iteration budget for the fragment loop
 *     uniform int   u_type;       user-pinned sub-type (or 0 if random);
 *                                 multibrot: the exponent in use (3..8)
 *     uniform float u_exposure;   brightness multiplier
 *     uniform float u_speed;      time scale (motion of c parameter, etc.)
 *     uniform vec4  u_params;     family-specific parameter block (see below)
 *     uniform vec4  u_view;       escape-time families: (cycle start time,
 *                                 log2 zoom per second, c.re, c.im)
 *     uniform vec4  u_map;        palette mapping (see FractMapping)
 *     uniform int   u_fx;         effect bits (see fract_fx_bits)
 *     uniform vec4  u_fxp;        effect parameters (see fract_fx_params_draw)
 *     uniform sampler2D u_palette_a; 256x1 RGBA8 cosine-gradient palette LUT
 *     uniform sampler2D u_palette_b;
 *     uniform float     u_palette_blend;
 *
 *     vec3 fract_palette(float t);   sample blended LUT
 *     vec3 fract_ramp(float t, float sat);  palette with saturation multiplier
 *     vec3 fract_escape_colour(float nu, float trap, float phase);
 *                                    shared escape-time colouring (see below)
 *     out vec4 frag_color;           write here
 *
 * The preamble text is `fractasia_frag_preamble` in fractasia_data.c, shared
 * by the driver and the GPU smoke test so the two cannot drift apart.
 *
 * The shader body must define `void main()` and write `frag_color`.
 *
 * The per-family parameter block (vec4 u_params) is laid out per
 * family. See the per-family entry below for the layout. The driver
 * pre-fills them from a deterministic xorshift RNG seeded with
 * `--seed`; user `--family`/`--type`/`--iterations` flags override
 * the relevant slots.
 *
 * This file is original to ncz-screensavers (Apache-2.0). It is
 * NOT derived from the Fractint source distribution.
 */

#ifndef FRACTASIA_H
#define FRACTASIA_H

#include <stdint.h>

/* Algorithm families. The order is the meson build slot index and the
 * enum value pushed to `u_family` (where supported). The driver
 * iterates [0..FRACT_NUM_FAMILIES) when compiling. */
enum fractasia_family_id {
    FRACT_ESCAPE_TIME = 0,
    FRACT_MULTIBROT,
    FRACT_BURNING_SHIP,
    FRACT_TRICORN,
    FRACT_JULIA,
    FRACT_QUATERNION,
    FRACT_NEWTON,
    FRACT_ATTRACTOR,
    FRACT_IFS,
    FRACT_PLASMA,
    FRACT_LSYSTEM,
    FRACT_LYAPUNOV,
    FRACT_CELLULAR,
    /* Absorbed from hyprsaver (MIT): see docs/fractasia/HYPRSAVER-LICENSE. */
    FRACT_JULIA_GLOW,
    FRACT_ORBIT_TRAP,
    FRACT_MOBIUS,
    FRACT_SHIP_GLOW,
    FRACT_NUM_FAMILIES,
    FRACT_AUTO = -1  /* user picked random / not pinned */
};

/* The number of distinct palette choices the driver exposes in
 * the --palette option. Must match the FRACT_PALETTES string in
 * gles3_fractasia.c. */
#define FRACT_PAL_COUNT 9

/* Cosine-gradient palette: col(t) = a + b * cos(2*pi * (c*t + d)), per channel
 * (the "IQ" form). Evaluated here, in the GL-free data file, so the generator
 * and its quality gate can be unit-tested without a GL context. */
typedef struct {
    float a[3], b[3], c[3], d[3];
} FractPaletteDef;

/* Evaluate the palette at t in [0,1], clamped to [0,1] per channel. `clipped`
 * (optional) is set to 1 when any channel had to be clamped. */
void fract_palette_eval(const FractPaletteDef *p, float t, float rgb[3], int *clipped);

/* Quality gate for a generated palette. The low end (t <= 0.3, which most
 * pixels of an escape-time picture use) must be dark-to-mid and coloured, not
 * pale; the full range must have bright accents, contrast and strong colour;
 * channel clipping is allowed but not dominant. Rejects the washed-out and
 * grey results a blind random draw produces, and the bright-start palettes
 * that made the first Orion O6N fractint run look like pink on white. */
int fract_palette_acceptable(const FractPaletteDef *p);

/* Palette kinds: the quality gate is relaxed per kind, so a deliberate
 * monochrome, film or pastel scheme passes while an accidental grey or pale
 * generated palette still fails. */
enum fract_palette_kind {
    FRACT_PK_COLOUR = 0,  /* the full gate: dark coloured low end, bright accents */
    FRACT_PK_MONO,        /* duotone with an accent: chroma may be low */
    FRACT_PK_PASTEL,      /* a brighter low end is the point */
    FRACT_PK_FILM         /* desaturated, muted contrast */
};

/* The named palette library: hand-picked cosine-gradient parameter sets,
 * each with its kind, drawn per cycle beside the generated palettes and
 * selectable with --palette=NAME. */
typedef struct {
    const char *name;
    FractPaletteDef def;
    int kind;
    int cygnus;  /* leans magenta / violet / cyan */
} FractPaletteLib;
#define FRACT_LIB_COUNT 13
extern const FractPaletteLib fractasia_palette_library[FRACT_LIB_COUNT];
const FractPaletteLib *fract_palette_library_find(const char *name);

/* The gate for a palette of `kind` (FRACT_PK_COLOUR is fract_palette_acceptable). */
int fract_palette_acceptable_kind(const FractPaletteDef *p, int kind);

/* Per-cycle palette source, drawn from `*rng`: a library palette (four draws
 * in ten), a generated palette forced to the Cygnus scheme (two in ten) or a
 * free generated palette (four in ten; itself Cygnus two draws in five).
 * `name` receives the library name, "cygnus" or "generated"; returns 1 when
 * the draw leans Cygnus.  Deterministic for a seed. */
int fract_palette_cycle(uint64_t *rng, FractPaletteDef *out, const char **name);

/* Draw a random palette from the xorshift64 state `*rng` (advanced in place):
 * a harmony scheme (analogous, complementary, triadic, free) with random hue,
 * brightness, amplitude and band count, retried until it passes the gate.
 * Returns the number of attempts used (1..FRACT_PAL_MAX_ATTEMPTS), or 0 when
 * none passed, in which case `out` is left unspecified and the caller falls
 * back to a named palette. */
#define FRACT_PAL_MAX_ATTEMPTS 48
int fract_palette_random(uint64_t *rng, FractPaletteDef *out);
/* The same, with the harmony scheme forced to Cygnus (force_cygnus != 0). */
int fract_palette_random_scheme(uint64_t *rng, FractPaletteDef *out, int force_cygnus);

/* ------------------------------------------------------------------- */
/* Look: palette mapping and effect recipes                             */
/* ------------------------------------------------------------------- */

/* How the palette is applied, drawn per cycle; the shader gets it as
 * u_map = (band scale * direction, posterise levels or 0, colour cycling in
 * palette units per second, interior palette position). */
typedef struct {
    float band_scale;   /* 0.6 .. 1.7: band period multiplier */
    int   posterise;    /* 0 smooth, else 4 .. 9 levels */
    float direction;    /* +1 or -1: reversed ramp */
    float cycle;        /* 0, or 0.01 .. 0.04 palette units per second */
    float interior_t;   /* palette position of the interior tint */
} FractMapping;
void fract_mapping_draw(uint64_t *rng, FractMapping *out);

/* Effect bits (u_fx).  The escape-time families honour them all; the other
 * families honour the post-effects (vignette, grain) and plasma the folds. */
enum fract_fx_bits {
    FRACT_FX_TRAP_POINT = 1 << 0,   /* orbit trap: distance to the origin */
    FRACT_FX_TRAP_LINE  = 1 << 1,   /* orbit trap: distance to the real axis */
    FRACT_FX_TRAP_CROSS = 1 << 2,   /* orbit trap: distance to either axis */
    FRACT_FX_STRIPE     = 1 << 3,   /* stripe-average colouring */
    FRACT_FX_DE         = 1 << 4,   /* distance-estimate outline (tracks dz: heavier) */
    FRACT_FX_EMBOSS     = 1 << 5,   /* derivative emboss / rim light */
    FRACT_FX_GLOW       = 1 << 6,   /* boundary glow from the distance estimate */
    FRACT_FX_CA         = 1 << 7,   /* chromatic aberration along the bands */
    FRACT_FX_VIGNETTE   = 1 << 8,
    FRACT_FX_GRAIN      = 1 << 9,   /* light film grain */
    FRACT_FX_KALEIDO    = 1 << 10,  /* mirror / kaleidoscope fold */
    FRACT_FX_WARP       = 1 << 11   /* slow domain-warp drift */
};

/* A curated recipe: a name, its effect bits and the lowest quality tier
 * (0 weak, 1 medium, 2 high) allowed to run it in style=auto. */
typedef struct {
    const char *name;
    int bits;
    int min_quality;
} FractRecipe;
#define FRACT_RECIPE_COUNT 14
extern const FractRecipe fractasia_recipes[FRACT_RECIPE_COUNT];
const FractRecipe *fract_recipe_find(const char *name);

/* Draw a recipe index for a cycle: among the recipes `quality` may run
 * (every recipe when `all` is set).  Deterministic for a seed. */
int fract_recipe_draw(uint64_t *rng, int quality, int all);

/* Effect parameters per cycle: u_fxp = (kaleidoscope sectors, warp
 * amplitude, grain amount, aberration amount). */
void fract_fx_params_draw(uint64_t *rng, float out[4]);

/* Cellular automaton (1D elementary rule) on a ring of FRACT_CA_W cells. The
 * automaton is stepped on the CPU, one row at a time, into a small ring
 * texture the shader only looks up. (The shader used to replay up to 200 rows
 * per pixel, with state packed in 64-bit words that GLSL's 32-bit ints cannot
 * hold, and clamped the row index so the picture degenerated into vertical bars.) */
/* The ring width is prime on purpose. Rules 90, 60, 102 and 150 are linear, and
 * on a ring whose width is a power of two they are nilpotent: every pattern
 * dies out after W/2 rows and the screen goes empty. */
#define FRACT_CA_W 251
#define FRACT_CA_H 128  /* rows kept resident; must exceed the rows on screen */
#define FRACT_CA_VISIBLE_ROWS 126 /* square cells at 16:9: 251 columns across the width */

/* First row for `rule`: a single live cell for the triangle-forming rules, and
 * a random row (density 1/2) for rules a single cell leaves boring (110, 184:
 * 184 moves a lone cell and draws a single line). `seed` makes it repeatable. */
void fract_ca_init_row(int rule, uint32_t seed, uint8_t row[FRACT_CA_W]);

/* One generation of `rule` (Wolfram numbering), wrapping at the ends. */
void fract_ca_step(int rule, const uint8_t in[FRACT_CA_W], uint8_t out[FRACT_CA_W]);

/* Whether the automatic picker may choose this family. Six families draw poorly
 * on real hardware (checked on Orion O6N 2026-10-03): ifs is a solid fill, newton and
 * quaternion are flat or washed out, lsystem is one stub line, lyapunov and
 * attractor are static-looking noise. They stay compiled and can be pinned with
 * --family=NAME, but the picker skips them. */
int fract_family_in_rotation(int fid);

/* Map a family ID to the .frag basename. The basename is looked up
 * first in the build tree (vendor/fractasia/glsl/), then in
 * /usr/share/ncz-screensavers/shaders/fractasia/. */
const char *fract_family_filename(int fid);

/* The fragment-shader preamble (version, precision, uniforms, palette and
 * colouring helpers) prepended to every family body. */
extern const char fractasia_frag_preamble[];

/* ------------------------------------------------------------------- */
/* View picker for the escape-time families                            */
/*                                                                     */
/* escape-time, multibrot, burning-ship, tricorn and julia used to draw  */
/* the whole default window every cycle: a flat far field, the set as  */
/* one blob, and the boundary (the only part with structure) a thin    */
/* line between them.  About one frame in five was dull.  Each cycle   */
/* now gets a VIEW: a window centre on the set boundary and a zoom      */
/* that keeps rising through the cycle, chosen on the CPU from a cheap  */
/* low-resolution probe of the same iteration the shader runs, and      */
/* accepted only when the probe shows enough boundary detail.           */
/* ------------------------------------------------------------------- */

typedef struct {
    float cx, cy;      /* window centre in the complex plane */
    float zoom;        /* 1 = the family's default window (half-height 1.5) */
    float zoom_rate;   /* log2 zoom gained per second of the cycle */
    float rot;         /* window rotation, radians */
    float cre, cim;    /* julia and multibrot: the c parameter; others unused */
    int   n;           /* multibrot: exponent 3..8; others 2 */
} FractView;

typedef struct {
    float interior;  /* fraction of probe cells that never escaped */
    float far;       /* fraction that escaped within the first 3 iterations */
    float boundary;  /* fraction of cells next to an interior/exterior change
                        or a large iteration jump */
    float central;   /* the same, over the middle 60% of the frame only */
    float spread;    /* standard deviation of log2(1 + iterations) over the
                        exterior cells */
} FractViewStats;

#define FRACT_PROBE_W 48
#define FRACT_PROBE_H 27
#define FRACT_PROBE_ITER 100   /* ceiling; the driver passes the tier's real budget */
#define FRACT_PROBE_ITER_MIN 32
#define FRACT_VIEW_MAX_ATTEMPTS 16
#define FRACT_VIEW_MAX_ZOOM 600.0f  /* float precision limit at 4K */

/* Whether the family is drawn through a FractView (the five above). */
int fract_view_family(int fid);

/* Iteration count of one point under the family's recurrence, with the same
 * float math as the shader: 0..max_iter-1 for an escape at that step, max_iter
 * when it never escaped.  escape-time, burning-ship and tricorn iterate the
 * point as c; julia and multibrot iterate the point as z0 with `v`'s c (and,
 * for multibrot, its exponent: the Julia set of z^n + c).  The Mandelbrot's
 * main cardioid and period-2 bulb are recognised analytically (as the shader
 * does), so interior points cost nothing. */
int fract_view_iterate(int fid, const FractView *v, float x, float y, int max_iter);

/* Probe a view on a FRACT_PROBE_W x FRACT_PROBE_H grid with an iteration
 * budget of `max_iter` (clamped to FRACT_PROBE_ITER_MIN..FRACT_PROBE_ITER)
 * and fill `st`.  Pass the budget the shader will actually run at the
 * cycle start: a point that needs more iterations than that is drawn as
 * interior, whatever the mathematics says, so the gate must judge the
 * picture the tier will show (on the weak class, 48). */
void fract_view_probe(int fid, const FractView *v, int max_iter, FractViewStats *st);

/* The quality gate: enough boundary detail, neither a flat far field nor
 * a screen of interior (and at least 30% working exterior between them),
 * and real variety in the iteration counts. */
int fract_view_acceptable(const FractViewStats *st);

/* Draw a view from `*rng` (advanced in place): a random boundary cell of the
 * family's default window, refined by zooming onto a boundary cell of that
 * view, retried until the gate passes.  Returns the attempts used (1..),
 * or 0 when none passed, in which case `out` holds a curated fallback view
 * for the family.  `n` is the multibrot exponent (ignored by the others);
 * julia and multibrot draw their c within a hair of the boundary of the
 * Mandelbrot / multibrot set, where the Julia sets are richest. */
int fract_view_pick(uint64_t *rng, int fid, int n, int max_iter, FractView *out);

/* Zoom at `t` seconds into the cycle, clamped to the precision limit. */
float fract_view_zoom_at(const FractView *v, double t);

/* The Wolfram rules we ship. Eight classic 1D rules that produce
 * recognisable visual structures; the cellular shader picks one
 * per family cycle. */
extern const int fractasia_wolfram_rules[8];

#endif /* FRACT_INT_SHADER_H */