/* SPDX-License-Identifier: Apache-2.0 */
/* gles3_fractasia.c — GPU fragment-shader screensaver implementing
 *                         the Fractint catalogue coverage described in
 *                         docs/fractasia/COVERAGE-MATRIX.md.
 *
 * One binary: `fractasia_gles3`. Seven algorithm families
 * (escape-time polynomial, Newton-Raphson, quaternion escape-time,
 * strange-attractor basin, IFS/plasma/L-system, Lyapunov, Wolfram CA)
 * are implemented as seven fragment shaders under
 * `vendor/fractasia/glsl/<family>.frag`. The driver compiles
 * all seven once at init, picks one per cycle using a deterministic
 * xorshift RNG keyed on the `--seed` option, and pushes the
 * per-shader uniforms every frame. The same seed + same override
 * produces the same first frame on every host.
 *
 * No Fractint source code, formulae, or assets are used. Every
 * algorithm is implemented from standard textbook treatments. See
 * docs/fractasia/LICENSE-ANALYSIS.md.
 *
 * Coverage and gap ledger: docs/fractasia/COVERAGE-MATRIX.md,
 * docs/fractasia/GAP-LEDGER.md. Curated parameter ranges:
 * docs/fractasia/PARAMETER-DOMAINS.md.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

#include <GLES3/gl32.h>

#include "ncz_gl.h"
#include "ncz_hack_shim.h"
#include "ncz_harness_cfg.h"
#include "ncz_options.h"
#include "ncz_platform.h"
#include "ncz_gpu_tier.h"

#include "fractasia.h"

/* ------------------------------------------------------------------- */
/* Per-shader-family option block. See fractasia.h.               */
/* ------------------------------------------------------------------- */

#define FRACT_FAMILIES "auto,random,escape-time,multibrot,burning-ship,tricorn,julia,quaternion,newton,attractor,ifs,plasma,lsystem,lyapunov,cellular,julia-glow,orbit-trap,mobius,ship-glow"
#define FRACT_QUALITY  "auto,low,medium,high,ultra"
#define FRACT_STYLE    "auto,classic,enhanced"
#define FRACT_PALETTES "random,stylized,ember,ink-aurora,opal,nocturne,cobalt,neon,magma,spectrum,fire,ice,aurora,sunset,ocean,synthwave,mono,pastel,oilslick,gold,nebula,acid,film"
#define FRACT_EFFECTS  "auto,none,plain,neon-edge,trap-point,trap-cross,stripes,kaleido,warp-drift,prism,film,outline,glow,trap-glow,kaleido-glow,emboss-de"
#define FRACT_PAL_RANDOM (-2)
#define FRACT_PAL_FADE_S 4.0 /* palette crossfade at each cycle; below the 5 s cycle minimum */

static const ncz_opt_def FRACT_OPTS[] = {
  /* name, type, default, min, max, choices, aliases, env_alias, label, description, group */
  {"style", NCZ_OPT_ENUM, "auto", 0, 0, FRACT_STYLE, NULL, NULL,
   "Style",
   "Which effect recipes a cycle may draw. auto: the recipes the GPU tier can afford (the light set on the weak class, the distance-estimate outline and glow from MEDIUM up). classic: no effects, smooth palette bands only. enhanced: every recipe whatever the tier.",
   "Look"},
  {"effect", NCZ_OPT_ENUM, "auto", 0, 0, FRACT_EFFECTS, NULL, NULL,
   "Effect recipe",
   "Pin one effect recipe for every cycle. auto (default): a different recipe each cycle, within --style. none: same as --style=classic. Recipes: plain, neon-edge (emboss), trap-point, trap-cross, stripes, kaleido, warp-drift, prism (chromatic aberration), film (grain + line trap); outline, glow, trap-glow (distance estimate; MEDIUM and up); kaleido-glow, emboss-de (HIGH and up).",
   "Look"},
  {"family", NCZ_OPT_ENUM, "auto", 0, 0, FRACT_FAMILIES, NULL, NULL,
   "Algorithm family",
   "Which GPU shader to draw. auto: a different one each cycle, from the eleven that draw well (escape-time, multibrot, burning-ship, tricorn, julia, plasma, cellular, julia-glow, orbit-trap, mobius, ship-glow). random: same as auto. Name a family to pin it; quaternion, newton, attractor, ifs, lsystem and lyapunov are only reachable this way.",
   "Algorithm"},
  {"type", NCZ_OPT_INT, "-1", -1, 64, "", NULL, NULL,
   "Sub-type",
   "Sub-type inside the family (e.g. multibrot exponent 3..8). -1 (default) = a different random pick inside the curated domain every cycle.",
   "Algorithm"},
  {"iterations", NCZ_OPT_INT, "0", 0, 512, "", NULL, NULL,
   "Iteration budget",
   "Hard cap on the fragment-shader iteration loop. 0 = pick from the family default scaled by --quality.",
   "Algorithm"},
  {"quality", NCZ_OPT_ENUM, "auto", 0, 0, FRACT_QUALITY, NULL, NULL,
   "Quality",
   "Resolution scale + iteration cap. auto picks by GPU tier.",
   "System"},
  {"speed", NCZ_OPT_FLOAT, "0.3", 0.02, 4, "", NULL, NULL,
   "Animation speed",
   "Time scale of the parameter animation (Julia c sweep, rotation, plasma drift, cellular scroll). 0.3 (default) is a calm, watchable pace: a Julia sweep takes about 4 minutes per loop. 1 is the old, fast pace.",
   "Motion"},
  {"cycle-interval", NCZ_OPT_FLOAT, "60", 5, 600, "", NULL, NULL,
   "Cycle interval (s)",
   "Seconds before the family picker chooses the next shader, with new parameters and (in random palette mode) a crossfade to a new palette.",
   "Motion"},
  {"palette", NCZ_OPT_ENUM, "random", 0, 0, FRACT_PALETTES, NULL, NULL,
   "Palette",
   "Colour palette. random (default): every cycle draws a new palette and crossfades to it: a named library scheme (four draws in ten), a generated Cygnus magenta/violet/cyan scheme (two) or a free generated harmony (four; itself Cygnus two draws in five); every draw passes a quality gate (dark coloured low end, bright accents, real contrast). The rest pin one: fire, ice, aurora, sunset, ocean, synthwave, mono, pastel, oilslick, gold, nebula, acid, film (the library) or stylized, ember, ink-aurora, opal, nocturne, cobalt, neon, magma, spectrum.",
   "Look"},
  {"hue-drift", NCZ_OPT_FLOAT, "1", 0, 4, "", NULL, NULL,
   "Hue drift",
   "How fast the whole picture's hue glides round the colour wheel, as a multiple of the default: one full turn about every four minutes at the default speed. Brightness is untouched, so dark grounds stay dark. 0 turns it off.",
   "Look"},
  {"exposure", NCZ_OPT_FLOAT, "1", 0.2, 4, "", NULL, NULL,
   "Exposure",
   "Brightness multiplier applied before tone mapping.",
   "Look"},
  {"seed", NCZ_OPT_INT, "0", 0, 4294967295.0, "", NULL, NULL,
   "Random seed",
   "Per-run deterministic seed. 0 = random.",
   "System"},
};

#define FRACT_NOPTS (sizeof FRACT_OPTS / sizeof FRACT_OPTS[0])

/* ------------------------------------------------------------------- */
/* Per-shader uniform layouts. The shader sources declare these exact  */
/* names; see vendor/fractasia/glsl/*.frag.                       */
/* ------------------------------------------------------------------- */

typedef struct {
    GLint u_time;
    GLint u_resolution;
    GLint u_frame;
    GLint u_seed;
    GLint u_iterations;
    GLint u_type;
    GLint u_exposure;
    GLint u_speed;
    /* Per-shader parameter block (family-specific, see fractasia.h). */
    GLint u_params;
    /* Escape-time view block (see fractasia.h). */
    GLint u_view;
    GLint u_map;
    GLint u_fx;
    GLint u_fxp;
    /* Palette LUT (always present). */
    GLint u_palette_a;
    GLint u_palette_b;
    GLint u_palette_blend;
    GLint u_hue;
    /* Cellular only: the CPU-stepped row ring. */
    GLint u_ca;
} FractLocs;

typedef struct {
    GLuint program;
    FractLocs locs;
    /* The "what does this shader need" parameters, in stable struct
     * order so the C side can write vec4(u_params.x, u_params.y, ...).
     * The actual GPU vec4 layout is in fractasia.h. */
    int   iters_lo, iters_hi; /* clamped by quality */
    float base_speed;
} FractShaderSlot;

/* ------------------------------------------------------------------- */
/* Driver state                                                         */
/* ------------------------------------------------------------------- */

typedef struct {
    /* GL programs, one per algorithm family. Order matches
     * fractasia_family_id in fractasia.h. */
    FractShaderSlot slot[FRACT_NUM_FAMILIES];

    GLuint vao;
    GLuint vbo;
    GLuint pal_tex[2];     /* [0] shown, [1] crossfade target */
    float  pal_blend;
    int    pal_fading;
    double pal_fade_t0;
    int    palette_random; /* 1 = a new palette per run and per cycle */
    int    pal_attempts;
    const char *pal_name;  /* the cycle's palette source, for the diag line */
    const FractPaletteLib *pal_lib; /* pinned library palette, or NULL */
    /* The cycle's look: palette mapping and effect recipe. */
    FractMapping mapping;
    int    recipe;         /* index into fractasia_recipes */
    int    fx_bits;        /* the recipe's bits as applied to this family */
    float  fxp[4];
    /* Per-cycle state. Filled once when a cycle begins, never per frame: the
     * fill draws from the RNG, so doing it per frame re-randomised every
     * unpinned parameter (and the seed vector) on every frame. */
    float  params[4];
    float  seedvec[4];
    int    params_family;
    /* Escape-time families: the cycle's view and its uniform block. */
    FractView view;
    float  viewvec[4];
    int    view_attempts;
    FractViewStats view_stats;

    /* Cellular automaton ring: rows made so far this cycle and the live row. */
    GLuint ca_tex;
    long   ca_rows_made;
    int    ca_rule;
    int    ca_gen;       /* bumps every cycle: forces a restart from row 0 */
    int    ca_seen_gen;
    uint8_t ca_row[FRACT_CA_W];
    double ca_t0;        /* seconds-since-start at the cycle's first row */

    /* Resolved user options. */
    int    style; /* 0 classic (no effects), 1 auto (by tier), 2 enhanced (all) */
    int    effect_pin; /* -1 auto, -2 none, else a recipe index */
    int    family; /* fractasia_family_id, or FRACT_AUTO for picker */
    int    type;
    int    quality; /* 0 low, 1 medium, 2 high, 3 ultra */
    int    palette_id;
    float  speed;
    float  exposure;
    float  hue_drift; /* multiple of the default drift rate; 0 = off */
    float  cycle_interval;
    int    iter_override; /* 0 = use family default scaled by quality */
    uint32_t seed;

    /* Runtime. */
    double start_time;
    int    current_family; /* which slot is active this cycle */
    double last_cycle_t;   /* time we last swapped families */
    unsigned long frame;

    /* Deterministic xorshift RNG state for picker + per-shader param. */
    uint64_t rng_s;

    int    width, height;
} FractState;

static FractState *FS;

static int fract_iter_base(const FractState *f);
static void fract_draw_look(FractState *f, int fid);

/* ------------------------------------------------------------------- */
/* Deterministic RNG (xorshift64*). Same sequence across hosts and      */
/* across runs with the same seed.                                      */
/* ------------------------------------------------------------------- */

static uint64_t fract_xs_next(uint64_t *s) {
    uint64_t x = *s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *s = x;
    return (x * 0x2545F4914F6CDD1Dull) >> 32;
}
static void fract_xs_seed(FractState *f, uint32_t seed) {
    uint64_t v = 0x9E3779B97F4A7C15ull ^ ((uint64_t)seed * 0xD1B54A32D192ED03ull);
    if (!v) v = 1;
    f->rng_s = v;
}
static float fract_rnd(FractState *f) {
    return (fract_xs_next(&f->rng_s) >> 8) * (1.0f / 16777216.0f);
}
static int fract_rnd_int(FractState *f, int n) {
    if (n <= 0) return 0;
    return (int)(fract_xs_next(&f->rng_s) % (uint32_t)n);
}

/* ------------------------------------------------------------------- */
/* Palette: cosine-gradient LUT baked from IQ's formula                 */
/*         col(t) = a + b·cos(2π·(c·t + d))                            */
/* Stored as 256x1 RGBA8. A and B both bound to the same LUT — the     */
/* helper function in the shader blends them by u_palette_blend.       */
/* ------------------------------------------------------------------- */

typedef FractPaletteDef PaletteDef;

static const PaletteDef fract_palettes[FRACT_PAL_COUNT] = {
  /* stylized — warm monochrome */
  {{0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 0.5f}, {0.0f, 0.10f, 0.20f}},
  /* ember — blackbody */
  {{0.5f, 0.2f, 0.05f}, {0.5f, 0.4f, 0.15f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.10f, 0.20f}},
  /* ink aurora — cool greens */
  {{0.1f, 0.3f, 0.4f}, {0.2f, 0.5f, 0.4f}, {0.5f, 1.0f, 0.7f}, {0.10f, 0.25f, 0.40f}},
  /* opal — pearl */
  {{0.7f, 0.6f, 0.7f}, {0.3f, 0.3f, 0.3f}, {2.0f, 1.5f, 1.0f}, {0.0f, 0.10f, 0.20f}},
  /* nocturne — deep blue */
  {{0.0f, 0.05f, 0.15f}, {0.2f, 0.4f, 0.7f}, {0.5f, 0.7f, 1.0f}, {0.50f, 0.20f, 0.25f}},
  /* cobalt — cyan/magenta */
  {{0.4f, 0.2f, 0.5f}, {0.5f, 0.4f, 0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.20f, 0.50f}},
  /* neon — hot pink/yellow */
  {{0.6f, 0.2f, 0.4f}, {0.5f, 0.5f, 0.2f}, {1.0f, 1.0f, 0.5f}, {0.0f, 0.15f, 0.30f}},
  /* magma — lava */
  {{0.3f, 0.05f, 0.0f}, {0.7f, 0.4f, 0.2f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.10f, 0.20f}},
  /* spectrum — full hue */
  {{0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {2.0f, 1.0f, 0.0f}, {0.5f, 0.2f, 0.0f}},
};

static void fract_upload_palette(GLuint tex, const PaletteDef *p) {
    unsigned char px[256 * 4];
    for (int i = 0; i < 256; i++) {
        float rgb[3];
        fract_palette_eval(p, (float)i / 255.0f, rgb, NULL);
        for (int ch = 0; ch < 3; ch++) px[i * 4 + ch] = (unsigned char)(rgb[ch] * 255.0f);
        px[i * 4 + 3] = 255;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static GLuint fract_make_palette_tex(const PaletteDef *p) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    fract_upload_palette(t, p);
    return t;
}

/* ------------------------------------------------------------------- */
/* File loading                                                         */
/* ------------------------------------------------------------------- */

static char *fract_read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "fractasia: cannot open %s\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) { free(buf); return NULL; }
    buf[len] = '\0';
    if (out_len) *out_len = (size_t)len;
    return buf;
}

static const char *fract_find_shader(const char *basename) {
    static char path[1024];
    const char *candidates[] = {
        "vendor/fractasia/glsl",
        "/usr/share/ncz-screensavers/shaders/fractasia",
        NULL,
    };
    for (int i = 0; candidates[i]; i++) {
        snprintf(path, sizeof path, "%s/%s", candidates[i], basename);
        FILE *f = fopen(path, "rb");
        if (f) { fclose(f); return path; }
    }
    return NULL;
}

/* ------------------------------------------------------------------- */
/* Shader compile/link                                                  */
/* ------------------------------------------------------------------- */

/* Shader preamble: version, precision, uniforms and the palette / colouring
 * helpers. The text lives in fractasia_data.c (fractasia_frag_preamble) so
 * the GPU smoke test compiles exactly what ships. */
static const char *fract_frag_preamble = fractasia_frag_preamble;

static GLuint fract_compile(GLenum stage, const char *src, const char *what) {
    GLuint sh = glCreateShader(stage);
    if (!sh) {
        fprintf(stderr, "fractasia: glCreateShader(%d) failed\n", (int)stage);
        return 0;
    }
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0}; GLsizei n = 0;
        glGetShaderInfoLog(sh, sizeof log - 1, &n, log);
        log[n] = 0;
        fprintf(stderr, "fractasia: %s compile failed: %s\n", what, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint fract_compile_frag(const char *body, const char *what) {
    size_t blen = strlen(body);
    size_t plen = strlen(fract_frag_preamble);
    char *full = (char *)malloc(plen + blen + 4);
    if (!full) return 0;
    memcpy(full, fract_frag_preamble, plen);
    memcpy(full + plen, body, blen + 1);
    GLuint sh = fract_compile(GL_FRAGMENT_SHADER, full, what);
    free(full);
    return sh;
}

static const char *fract_vert_src =
    "#version 320 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

static int fract_link(GLuint vs, GLuint fs, const char *what) {
    GLuint p = glCreateProgram();
    if (!p) return 0;
    glAttachShader(p, vs); glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0}; GLsizei n = 0;
        glGetProgramInfoLog(p, sizeof log - 1, &n, log);
        log[n] = 0;
        fprintf(stderr, "fractasia: %s link failed: %s\n", what, log);
        glDeleteProgram(p);
        return 0;
    }
    return (int)p;
}

static int fract_compile_family(int fid) {
    const char *file = fract_family_filename(fid);
    if (!file) return -1;
    const char *path = fract_find_shader(file);
    if (!path) {
        fprintf(stderr, "fractasia: shader file for family %d (%s) not found\n",
                fid, file);
        return -1;
    }
    size_t len = 0;
    char *body = fract_read_file(path, &len);
    if (!body) return -1;
    GLuint vs = fract_compile(GL_VERTEX_SHADER, fract_vert_src, file);
    GLuint fs = fract_compile_frag(body, file);
    free(body);
    if (!vs || !fs) {
        fprintf(stderr, "fractasia: %s: compile failure, family %d disabled\n",
                file, fid);
        FS->slot[fid].program = 0;
        return -1;
    }
    GLuint p = (GLuint)fract_link(vs, fs, file);
    if (!p) { FS->slot[fid].program = 0; return -1; }
    FS->slot[fid].program = p;
    FractLocs *L = &FS->slot[fid].locs;
    L->u_time        = glGetUniformLocation(p, "u_time");
    L->u_resolution  = glGetUniformLocation(p, "u_resolution");
    L->u_frame       = glGetUniformLocation(p, "u_frame");
    L->u_seed        = glGetUniformLocation(p, "u_seed");
    L->u_iterations  = glGetUniformLocation(p, "u_iterations");
    L->u_type        = glGetUniformLocation(p, "u_type");
    L->u_exposure    = glGetUniformLocation(p, "u_exposure");
    L->u_speed       = glGetUniformLocation(p, "u_speed");
    L->u_params      = glGetUniformLocation(p, "u_params");
    L->u_view        = glGetUniformLocation(p, "u_view");
    L->u_map         = glGetUniformLocation(p, "u_map");
    L->u_fx          = glGetUniformLocation(p, "u_fx");
    L->u_fxp         = glGetUniformLocation(p, "u_fxp");
    L->u_palette_a   = glGetUniformLocation(p, "u_palette_a");
    L->u_palette_b   = glGetUniformLocation(p, "u_palette_b");
    L->u_palette_blend = glGetUniformLocation(p, "u_palette_blend");
    L->u_hue         = glGetUniformLocation(p, "u_hue");
    L->u_ca          = glGetUniformLocation(p, "u_ca");

    /* Family-specific quality caps: enforced at draw time. */
    FS->slot[fid].iters_lo = 16;
    FS->slot[fid].iters_hi = 256;
    FS->slot[fid].base_speed = 1.0f;
    return 0;
}

/* ------------------------------------------------------------------- */
/* Option parsing                                                       */
/* ------------------------------------------------------------------- */

static int fract_style_id(const char *s) {
    if (!strcmp(s, "classic")) return 0;
    if (!strcmp(s, "enhanced")) return 2;
    return 1; /* auto */
}
static int fract_effect_id(const char *s) {
    if (!s || !strcmp(s, "auto")) return -1;
    if (!strcmp(s, "none")) return -2;
    const FractRecipe *r = fract_recipe_find(s);
    return r ? (int)(r - fractasia_recipes) : -1;
}
static int fract_family_id(const char *s) {
    if (!strcmp(s, "auto") || !strcmp(s, "random")) return FRACT_AUTO;
    if (!strcmp(s, "escape-time")) return FRACT_ESCAPE_TIME;
    if (!strcmp(s, "multibrot"))   return FRACT_MULTIBROT;
    if (!strcmp(s, "burning-ship")) return FRACT_BURNING_SHIP;
    if (!strcmp(s, "tricorn"))     return FRACT_TRICORN;
    if (!strcmp(s, "julia"))       return FRACT_JULIA;
    if (!strcmp(s, "quaternion"))  return FRACT_QUATERNION;
    if (!strcmp(s, "newton"))      return FRACT_NEWTON;
    if (!strcmp(s, "attractor"))   return FRACT_ATTRACTOR;
    if (!strcmp(s, "ifs"))         return FRACT_IFS;
    if (!strcmp(s, "plasma"))      return FRACT_PLASMA;
    if (!strcmp(s, "lsystem"))     return FRACT_LSYSTEM;
    if (!strcmp(s, "lyapunov"))    return FRACT_LYAPUNOV;
    if (!strcmp(s, "cellular"))    return FRACT_CELLULAR;
    if (!strcmp(s, "julia-glow"))  return FRACT_JULIA_GLOW;
    if (!strcmp(s, "orbit-trap"))  return FRACT_ORBIT_TRAP;
    if (!strcmp(s, "mobius"))      return FRACT_MOBIUS;
    if (!strcmp(s, "ship-glow"))   return FRACT_SHIP_GLOW;
    return FRACT_AUTO;
}
static const char *const fract_pal_names[FRACT_PAL_COUNT] = {
    "stylized", "ember", "ink-aurora", "opal", "nocturne", "cobalt", "neon", "magma", "spectrum"
};
static int fract_pal_id(const char *s) {
    if (!s || !strcmp(s, "random")) return FRACT_PAL_RANDOM;
    if (!strcmp(s, "ink aurora")) s = "ink-aurora"; /* the pre-fix spelling */
    for (int i = 0; i < FRACT_PAL_COUNT; i++) if (!strcmp(s, fract_pal_names[i])) return i;
    return FRACT_PAL_RANDOM;
}
static int fract_quality_id(const char *s) {
    if (!strcmp(s, "low")) return 0;
    if (!strcmp(s, "medium")) return 1;
    if (!strcmp(s, "high")) return 2;
    if (!strcmp(s, "ultra")) return 3;
    return -1; /* auto */
}

static uint32_t fract_seed(void) {
    long sd = ncz_opts_get_int(ncz_harness_opts(), "seed");
    if (sd > 0) return (uint32_t)sd;
    uint32_t v = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t r = read(fd, &v, sizeof v);
        (void)r;
        close(fd);
    }
    if (!v) v = (uint32_t)time(NULL) ^ (uint32_t)getpid();
    return v;
}

static int fract_auto_tier_quality(void) {
    /* Same source as cs_common.h::cs_style: tier LOW=0 -> classic,
     * MEDIUM+ -> enhanced; quality tier = LOW/MEDIUM/HIGH from
     * the user's choice or auto. */
    ncz_gpu_tier_init();
    const ncz_gpu_tier_t *t = ncz_gpu_tier_current();
    if (!t) return 1; /* medium */
    if (t->tier >= NCZ_TIER_HIGH) return 2;
    if (t->tier >= NCZ_TIER_MEDIUM) return 1;
    return 0;
}

static void fract_resolve_opts(FractState *f) {
    const ncz_opts *o = ncz_harness_opts();
    f->style = fract_style_id(ncz_opts_get(o, "style"));
    f->effect_pin = fract_effect_id(ncz_opts_get(o, "effect"));
    f->family = fract_family_id(ncz_opts_get(o, "family"));
    f->type = (int)ncz_opts_get_int(o, "type");
    f->quality = fract_quality_id(ncz_opts_get(o, "quality"));
    if (f->quality < 0) f->quality = fract_auto_tier_quality();
    f->speed = (float)ncz_opts_get_float(o, "speed");
    f->exposure = (float)ncz_opts_get_float(o, "exposure");
    f->hue_drift = (float)ncz_opts_get_float(o, "hue-drift");
    f->cycle_interval = (float)ncz_opts_get_float(o, "cycle-interval");
    f->iter_override = (int)ncz_opts_get_int(o, "iterations");
    f->pal_lib = fract_palette_library_find(ncz_opts_get(o, "palette"));
    f->palette_id = f->pal_lib ? 0 : fract_pal_id(ncz_opts_get(o, "palette"));
    f->palette_random = !f->pal_lib && (f->palette_id == FRACT_PAL_RANDOM);
    if (f->palette_random) f->palette_id = 0; /* fallback when a draw fails the gate */
    f->pal_name = f->pal_lib ? f->pal_lib->name : (f->palette_random ? "random" : fract_pal_names[f->palette_id]);
    f->seed = fract_seed();
}

/* ------------------------------------------------------------------- */
/* Family picker (deterministic)                                       */
/* ------------------------------------------------------------------- */

static int fract_pick_family(FractState *f) {
    /* If user pinned, honour it. */
    if (f->family >= 0 && f->family < FRACT_NUM_FAMILIES) {
        if (f->slot[f->family].program) return f->family;
    }
    /* Otherwise random through the families that compiled. The picker
     * deliberately visits every family in a long cycle so the
     * visual diversity is high — the user sees all 13 family labels
     * (including the "random" alias) and several of them per minute. */
    int tried = 0;
    while (tried < FRACT_NUM_FAMILIES * 4) {
        int fid = fract_rnd_int(f, FRACT_NUM_FAMILIES);
        if (f->slot[fid].program && fract_family_in_rotation(fid)) return fid;
        tried++;
    }
    /* None of the rotation families compiled: take anything that did. */
    for (int fid = 0; fid < FRACT_NUM_FAMILIES; fid++)
        if (f->slot[fid].program) return fid;
    return FRACT_ESCAPE_TIME;
}

/* ------------------------------------------------------------------- */
/* Per-shader parameter computation                                     */
/*                                                                     */
/* Each family has a parameter block (vec4) that's constant for the    */
/* family cycle. The block is filled at cycle start, written once per  */
/* frame as a uniform. The math is on the GPU; this is just the        */
/* orchestrator.                                                       */
/* ------------------------------------------------------------------- */

static void fract_fill_params(FractState *f, int fid, float out_params[4]) {
    out_params[0] = out_params[1] = out_params[2] = out_params[3] = 0.0f;
    int type = f->type;
    if (fract_view_family(fid)) {
        /* escape-time, multibrot, burning-ship, tricorn, julia: a view on
         * the set boundary from the CPU probe (fractasia_data.c).  The
         * sub-type is the multibrot exponent (3..8); the other four ignore it.
         * u_params = (centre, zoom at cycle start, rotation);
         * u_view   = (cycle start time, log2 zoom per second, c.re, c.im). */
        int n = (type >= 0) ? (type % 6) + 3 : 3 + fract_rnd_int(f, 6);
        int budget = fract_iter_base(f);
        f->view_attempts = fract_view_pick(&f->rng_s, fid, n, budget, &f->view);
        fract_view_probe(fid, &f->view, budget, &f->view_stats);
        out_params[0] = f->view.cx;
        out_params[1] = f->view.cy;
        out_params[2] = f->view.zoom;
        out_params[3] = f->view.rot;
        f->viewvec[0] = (float)(ncz_now() - f->start_time);
        f->viewvec[1] = f->view.zoom_rate;
        f->viewvec[2] = f->view.cre;   /* julia, multibrot: c (multibrot's n rides in u_type) */
        f->viewvec[3] = f->view.cim;
        return;
    }
    switch (fid) {
    case FRACT_QUATERNION: {
        /* Quaternion c = (a, b, c, d). Pick a unit quaternion. */
        float theta = fract_rnd(f) * 6.2832f;
        float phi   = fract_rnd(f) * 3.1415f;
        float psi   = fract_rnd(f) * 6.2832f;
        out_params[0] = sinf(phi) * cosf(theta);
        out_params[1] = sinf(phi) * sinf(theta);
        out_params[2] = cosf(phi) * sinf(psi) * 0.5f;
        out_params[3] = cosf(phi) * cosf(psi) * 0.5f;
        break;
    }
    case FRACT_NEWTON: {
        int deg = (type >= 0) ? ((type % 5) + 3) : 3 + fract_rnd_int(f, 5);
        /* deg 3..7 */
        out_params[0] = (float)deg;
        out_params[1] = 0.05f + fract_rnd(f) * 0.10f; /* jitter */
        out_params[2] = 4.0f;                         /* step clamp */
        out_params[3] = 0.0f;
        break;
    }
    case FRACT_ATTRACTOR: {
        /* Sub-type 0..4 selects the attractor. */
        int at = (type >= 0) ? (type % 5) : fract_rnd_int(f, 5);
        out_params[0] = (float)at;
        out_params[1] = 10.0f + fract_rnd(f) * 18.0f;  /* rho */
        out_params[2] = 2.0f + fract_rnd(f) * 4.0f;    /* strength */
        out_params[3] = 0.0f;
        break;
    }
    case FRACT_IFS: {
        int sub = (type >= 0) ? (type % 4) : fract_rnd_int(f, 4);
        out_params[0] = (float)sub;
        out_params[1] = 0.4f + fract_rnd(f) * 0.4f;    /* roughness */
        out_params[2] = 1.0f;
        out_params[3] = 0.0f;
        break;
    }
    case FRACT_PLASMA:
        out_params[0] = 0.4f + fract_rnd(f) * 0.4f;     /* roughness */
        out_params[1] = fract_rnd(f) * 6.2832f;
        out_params[2] = fract_rnd(f) * 6.2832f;
        out_params[3] = fract_rnd(f) * 6.2832f;
        break;
    case FRACT_LSYSTEM: {
        int sub = (type >= 0) ? (type % 4) : fract_rnd_int(f, 4);
        out_params[0] = (float)sub;
        out_params[1] = 0.3f + fract_rnd(f) * 0.4f;    /* angle */
        out_params[2] = 1.5f + fract_rnd(f) * 1.5f;    /* scale */
        out_params[3] = 0.0f;
        break;
    }
    case FRACT_LYAPUNOV:
        out_params[0] = 3.4f + fract_rnd(f) * 0.6f;
        out_params[1] = 3.4f + fract_rnd(f) * 0.6f;
        out_params[2] = (float)((type >= 0) ? (type % 8) + 4 : 4 + fract_rnd_int(f, 12));
        out_params[3] = 0.0f;
        break;
    case FRACT_CELLULAR: {
        int rule = fractasia_wolfram_rules[(type >= 0) ? (type % 8) : fract_rnd_int(f, 8)];
        out_params[0] = (float)rule;
        out_params[1] = fract_rnd(f);
        out_params[2] = 40.0f * f->speed; /* rows per second: 40 at speed 1, 12 at the 0.3 default (about 95 px/s) */
        out_params[3] = (float)(ncz_now() - f->start_time); /* cycle start, for row 0 */
        break;
    }
    case FRACT_JULIA_GLOW:
        out_params[0] = fract_rnd(f) * 6.2832f; /* start phase of c */
        out_params[1] = fract_rnd(f) * 6.2832f; /* pan and zoom phase */
        break;
    case FRACT_ORBIT_TRAP:
        out_params[0] = fract_rnd(f) * 6.2832f;          /* start phase of c */
        out_params[1] = 0.52f + fract_rnd(f) * 0.10f;    /* c and trap radius */
        out_params[2] = fract_rnd(f) * 6.2832f;          /* start phase of the traps */
        break;
    case FRACT_MOBIUS:
        out_params[0] = fract_rnd(f) * 6.2832f; /* start position along the ribbon */
        out_params[1] = fract_rnd(f) * 6.2832f; /* start roll */
        break;
    case FRACT_SHIP_GLOW:
        out_params[0] = fract_rnd(f) * 6.2832f;          /* start phase of c */
        out_params[1] = 0.015f + fract_rnd(f) * 0.009f;  /* orbit radius */
        break;
    default:
        break;
    }
}

static void fract_fill_cycle_state(FractState *f, int fid) {
    fract_fill_params(f, fid, f->params);
    for (int k = 0; k < 4; k++) {
        if (!isfinite(f->params[k])) f->params[k] = 0.0f;
        if (f->params[k] > 1e6f) f->params[k] = 1e6f;
        if (f->params[k] < -1e6f) f->params[k] = -1e6f;
        f->seedvec[k] = fract_rnd(f);
    }
    f->params_family = fid;
    f->ca_gen++;
    fract_draw_look(f, fid);
}

/* Keep the cellular ring ahead of what the shader can reach this frame. */
static void fract_ca_update(FractState *f, double t) {
    if (!f->ca_tex) {
        glGenTextures(1, &f->ca_tex);
        glBindTexture(GL_TEXTURE_2D, f->ca_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FRACT_CA_W, FRACT_CA_H, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
        f->ca_seen_gen = -1;
    }
    if (f->ca_seen_gen != f->ca_gen) { /* a new cycle: start over from row 0 */
        f->ca_seen_gen = f->ca_gen;
        f->ca_rule = (int)(f->params[0] + 0.5f);
        f->ca_rows_made = 0;
        /* Start one screen into the automaton: the pattern has grown across
         * the ring before the first row is shown, instead of a lone cell
         * widening on an empty field for the first ten seconds. */
        f->ca_t0 = (double)f->params[3] - (double)FRACT_CA_VISIBLE_ROWS / (f->params[2] > 0.0f ? (double)f->params[2] : 12.0);
        fract_ca_init_row(f->ca_rule, (uint32_t)(f->seedvec[1] * 4294967040.0f), f->ca_row);
    }
    double scroll = (t - f->ca_t0) * (double)f->params[2];
    if (scroll < 0.0) scroll = 0.0;
    long need = (long)scroll + FRACT_CA_VISIBLE_ROWS + 2;
    glBindTexture(GL_TEXTURE_2D, f->ca_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    while (f->ca_rows_made <= need) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, (int)(f->ca_rows_made % FRACT_CA_H), FRACT_CA_W, 1,
                        GL_RED, GL_UNSIGNED_BYTE, f->ca_row);
        uint8_t next[FRACT_CA_W];
        fract_ca_step(f->ca_rule, f->ca_row, next);
        memcpy(f->ca_row, next, sizeof next);
        f->ca_rows_made++;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* The cycle's palette: a library scheme, a Cygnus or a free generated one
 * (fract_palette_cycle), or the pinned palette. */
static PaletteDef fract_draw_palette(FractState *f) {
    PaletteDef p;
    if (f->pal_lib) { f->pal_attempts = 1; return f->pal_lib->def; }
    fract_palette_cycle(&f->rng_s, &p, &f->pal_name);
    f->pal_attempts = 1;
    return p;
}

/* The cycle's look: mapping, recipe (within the style and the tier) and
 * effect parameters.  Drawn after the family's parameters so the RNG order
 * stays fixed. */
static void fract_draw_look(FractState *f, int fid) {
    fract_mapping_draw(&f->rng_s, &f->mapping);
    int r = fract_recipe_draw(&f->rng_s, f->quality, f->style == 2);
    if (f->style == 0 || f->effect_pin == -2) r = 0;
    if (f->effect_pin >= 0) r = f->effect_pin;
    f->recipe = r;
    f->fx_bits = fractasia_recipes[r].bits;
    /* The kaleidoscope folds the screen into one wedge about its centre: on
     * the Julia families and plasma structure surrounds the centre, on the
     * parameter-space families the wedge may hold none, so the fold is
     * dropped there (the rest of the recipe stays). */
    if (fid != FRACT_JULIA && fid != FRACT_MULTIBROT && fid != FRACT_PLASMA) f->fx_bits &= ~FRACT_FX_KALEIDO;
    fract_fx_params_draw(&f->rng_s, f->fxp);
}

static void fract_begin_cycle(FractState *f, double now) {
    fract_fill_cycle_state(f, f->current_family);
    if (!f->palette_random) return;
    PaletteDef next = fract_draw_palette(f);
    fract_upload_palette(f->pal_tex[1], &next);
    f->pal_fading = 1;
    f->pal_fade_t0 = now;
    f->pal_blend = 0.0f;
    fprintf(stderr, "[diag] fractasia: cycle family=%d palette=%s\n", f->current_family, f->pal_name);
}

static void fract_log_look(const FractState *f) {
    fprintf(stderr,
            "[diag] fractasia: look palette=%s recipe=%s fx=%d map=(scale %.1f dir %+.0f poster %d cycle %.3f interior %.2f)\n",
            f->pal_name, fractasia_recipes[f->recipe].name, f->fx_bits, f->mapping.band_scale, f->mapping.direction,
            f->mapping.posterise, f->mapping.cycle, f->mapping.interior_t);
}

static void fract_log_view(const FractState *f, int fid) {
    if (!fract_view_family(fid)) return;
    fprintf(stderr,
            "[diag] fractasia: view family=%d centre=(%.6f,%.6f) zoom=%.2f rate=%.4f rot=%.2f "
            "c=(%.4f,%.4f) n=%d attempts=%d boundary=%.3f central=%.3f interior=%.3f far=%.3f spread=%.3f\n",
            fid, f->view.cx, f->view.cy, f->view.zoom, f->view.zoom_rate, f->view.rot,
            f->view.cre, f->view.cim, f->view.n, f->view_attempts,
            f->view_stats.boundary, f->view_stats.central, f->view_stats.interior, f->view_stats.far,
            f->view_stats.spread);
}

static void fract_advance_fade(FractState *f, double now) {
    if (!f->pal_fading) return;
    double u = (now - f->pal_fade_t0) / FRACT_PAL_FADE_S;
    if (u >= 1.0) {
        GLuint t = f->pal_tex[0];
        f->pal_tex[0] = f->pal_tex[1];
        f->pal_tex[1] = t;
        f->pal_blend = 0.0f;
        f->pal_fading = 0;
        return;
    }
    f->pal_blend = (float)(u * u * (3.0 - 2.0 * u));
}

/* The iteration budget at a cycle's start: the user's override, else the
 * quality tier's base (low 48, medium 96, high 192, ultra 256). */
static int fract_iter_base(const FractState *f) {
    if (f->iter_override > 0) return f->iter_override;
    switch (f->quality) {
    case 0: return 48;
    case 1: return 96;
    case 2: return 192;
    case 3: return 256;
    default: return 128;
    }
}

static int fract_iter_cap(FractState *f, int fid, double t) {
    if (f->iter_override > 0) {
        if (f->iter_override < FS->slot[fid].iters_lo) return FS->slot[fid].iters_lo;
        if (f->iter_override > FS->slot[fid].iters_hi) return FS->slot[fid].iters_hi;
        return f->iter_override;
    }
    int base = fract_iter_base(f);
    /* Deeper views need more iterations to resolve their boundary; grow the
     * budget with the zoom depth (+30% per doubling), capped at x2 of the
     * tier's base on the weak class and x2.5 above it, so a long cycle never
     * runs the fragment loop past what the GPU paid for at the start. */
    if (fract_view_family(fid)) {
        double z = fract_view_zoom_at(&f->view, t - (double)f->viewvec[0]);
        double grow = 1.0 + 0.30 * log2(z > 1.0 ? z : 1.0);
        double cap = f->quality == 0 ? 2.0 : 2.5;
        base = (int)((double)base * (grow < cap ? grow : cap));
    }
    if (base < FS->slot[fid].iters_lo) return FS->slot[fid].iters_lo;
    if (base > FS->slot[fid].iters_hi) return FS->slot[fid].iters_hi;
    return base;
}

/* ------------------------------------------------------------------- */
/* Harness integration                                                  */
/* ------------------------------------------------------------------- */

const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    *n = FRACT_NOPTS;
    *prefix = "NCZ_FRACTASIA_";
    *group = "fractasia";
    return FRACT_OPTS;
}

static void init_fractint(ModeInfo *mi) {
    if (FS) return;
    FS = (FractState *)calloc(1, sizeof(FractState));
    if (!FS) { fprintf(stderr, "fractasia: out of memory\n"); return; }
    FS->width = mi->xgwa.width;
    FS->height = mi->xgwa.height;
    fract_resolve_opts(FS);
    fract_xs_seed(FS, FS->seed);

    glGenVertexArrays(1, &FS->vao);
    glBindVertexArray(FS->vao);
    glGenBuffers(1, &FS->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, FS->vbo);
    static const float verts[] = {
        -1.f, -1.f,  1.f, -1.f, -1.f,  1.f,
        -1.f,  1.f,  1.f, -1.f,  1.f,  1.f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArray(0);

    {
        PaletteDef first = FS->palette_random ? fract_draw_palette(FS) : fract_palettes[FS->palette_id];
        FS->pal_tex[0] = fract_make_palette_tex(&first);
        FS->pal_tex[1] = fract_make_palette_tex(&first);
    }

    for (int fid = 0; fid < FRACT_NUM_FAMILIES; fid++) {
        if (fract_compile_family(fid) != 0) {
            /* slot stays zeroed, picker will skip it. */
        }
    }

    FS->start_time = ncz_now();
    FS->last_cycle_t = FS->start_time;
    FS->frame = 0;
    FS->current_family = fract_pick_family(FS);
    fract_fill_cycle_state(FS, FS->current_family);
    fract_log_view(FS, FS->current_family);
    fract_log_look(FS);
    fprintf(stderr,
            "[diag] fractasia: seed=%u family=%d style=%s palette=%s "
            "attempts=%d speed=%.2f cycle=%.0fs quality=%d size=%dx%d\n",
            FS->seed, FS->current_family,
            FS->style == 0 ? "classic" : FS->style == 2 ? "enhanced" : "auto",
            FS->pal_name,
            FS->pal_attempts,
            FS->speed, FS->cycle_interval, FS->quality, FS->width, FS->height);
}

static void reshape_fractint(ModeInfo *mi, int w, int h) {
    if (!FS) return;
    FS->width = w; FS->height = h;
    glViewport(0, 0, w, h);
}

static void draw_fractint(ModeInfo *mi) {
    if (!FS) return;
    double now = ncz_now();
    double t = now - FS->start_time;
    FS->frame++;

    /* Family cycling: deterministic (RNG-driven), advance every
    // cycle_interval; keep family constant within a cycle so the
    // parameters don't snap mid-render. */
    if ((now - FS->last_cycle_t) >= FS->cycle_interval) {
        FS->last_cycle_t = now;
        FS->current_family = fract_pick_family(FS);
        fract_begin_cycle(FS, now);
        fract_log_view(FS, FS->current_family);
        fract_log_look(FS);
    }
    fract_advance_fade(FS, now);
    int fid = FS->current_family;
    if (fid < 0 || fid >= FRACT_NUM_FAMILIES || !FS->slot[fid].program) {
        /* fallback: pick anything that compiled */
        for (int i = 0; i < FRACT_NUM_FAMILIES; i++)
            if (FS->slot[i].program) { fid = i; break; }
        if (fid < 0 || !FS->slot[fid].program) return;
    }

    /* Per-cycle state, constant across the frames of a cycle. */
    if (fid != FS->params_family) fract_fill_cycle_state(FS, fid);
    const float *params = FS->params;
    int iters = fract_iter_cap(FS, fid, t);

    /* iSeed: four draws taken once per cycle, used by every family for
     * sub-cell jitter. */
    const float *seedvec = FS->seedvec;

    FractShaderSlot *s = &FS->slot[fid];
    glUseProgram(s->program);

    /* Cellular: upload any new rows BEFORE the palette textures are bound below.
     * The upload leaves texture unit 0 unbound, which would otherwise discard the
     * palette and draw everything black. */
    if (fid == FRACT_CELLULAR && s->locs.u_ca >= 0) fract_ca_update(FS, t);

    if (s->locs.u_time >= 0)       glUniform1f(s->locs.u_time, (float)t);
    if (s->locs.u_resolution >= 0) glUniform3f(s->locs.u_resolution,
                                                (float)FS->width,
                                                (float)FS->height,
                                                1.0f);
    if (s->locs.u_frame >= 0)      glUniform1i(s->locs.u_frame, (int)FS->frame);
    if (s->locs.u_seed >= 0)       glUniform4fv(s->locs.u_seed, 1, seedvec);
    if (s->locs.u_iterations >= 0) glUniform1i(s->locs.u_iterations, iters);
    if (s->locs.u_type >= 0)
        glUniform1i(s->locs.u_type, fid == FRACT_MULTIBROT ? FS->view.n : (FS->type >= 0 ? FS->type : 0));
    if (s->locs.u_exposure >= 0)   glUniform1f(s->locs.u_exposure, FS->exposure);
    if (s->locs.u_hue >= 0)
        glUniform1f(s->locs.u_hue, (float)fmod(t * 0.0873 * (double)FS->speed * (double)FS->hue_drift, 6.283185307179586));
    if (s->locs.u_speed >= 0)      glUniform1f(s->locs.u_speed, FS->speed);
    if (s->locs.u_params >= 0)     glUniform4fv(s->locs.u_params, 1, params);
    if (s->locs.u_view >= 0)       glUniform4fv(s->locs.u_view, 1, FS->viewvec);
    if (s->locs.u_map >= 0)
        glUniform4f(s->locs.u_map, FS->mapping.band_scale * FS->mapping.direction,
                    (float)FS->mapping.posterise, FS->mapping.cycle, FS->mapping.interior_t);
    if (s->locs.u_fx >= 0)         glUniform1i(s->locs.u_fx, FS->fx_bits);
    if (s->locs.u_fxp >= 0)        glUniform4fv(s->locs.u_fxp, 1, FS->fxp);

    /* Palette LUT. The shader expects u_palette_a on texture unit 0
     * and u_palette_b on unit 1. */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, FS->pal_tex[0]);
    if (s->locs.u_palette_a >= 0) glUniform1i(s->locs.u_palette_a, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, FS->pal_tex[1]);
    if (s->locs.u_palette_b >= 0) glUniform1i(s->locs.u_palette_b, 1);
    glActiveTexture(GL_TEXTURE0);
    if (s->locs.u_palette_blend >= 0) glUniform1f(s->locs.u_palette_blend, FS->pal_blend);
    if (fid == FRACT_CELLULAR && s->locs.u_ca >= 0) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, FS->ca_tex);
        glUniform1i(s->locs.u_ca, 2);
        glActiveTexture(GL_TEXTURE0);
    }

    glBindVertexArray(FS->vao);
    glViewport(0, 0, FS->width, FS->height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

static void free_fractint(ModeInfo *mi) {
    if (!FS) return;
    for (int fid = 0; fid < FRACT_NUM_FAMILIES; fid++) {
        if (FS->slot[fid].program) glDeleteProgram(FS->slot[fid].program);
    }
    if (FS->vao) glDeleteVertexArrays(1, &FS->vao);
    if (FS->vbo) glDeleteBuffers(1, &FS->vbo);
    if (FS->pal_tex[0]) glDeleteTextures(1, &FS->pal_tex[0]);
    if (FS->pal_tex[1]) glDeleteTextures(1, &FS->pal_tex[1]);
    if (FS->ca_tex) glDeleteTextures(1, &FS->ca_tex);
    free(FS);
    FS = NULL;
}

static Bool fractasia_handle_event(ModeInfo *mi, XEvent *e) {
    (void)mi; (void)e;
    return False;
}

static void release_fractint(ModeInfo *mi) { (void)mi; }

struct xscreensaver_function_table fractasia_xscreensaver_function_table = {
    .name = "fractasia", .class_ = "FractintShader",
    .init_cb = init_fractint, .draw_cb = draw_fractint, .reshape_cb = reshape_fractint,
    .event_cb = fractasia_handle_event, .free_cb = free_fractint,
    .release_cb = release_fractint,
    .opts = NULL,
    .defaults_str = "",
};