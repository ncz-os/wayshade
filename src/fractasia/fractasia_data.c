/* SPDX-License-Identifier: Apache-2.0 */
/* fractasia_data.c — non-shader data tables for the Fractint
 *                          screensaver. Original to ncz-screensavers
 *                          (Apache-2.0).
 *
 *  - fract_family_filename: maps each family ID to its .frag basename.
 *  - fractasia_frag_preamble: the shader preamble shared by the driver and
 *    the GPU smoke test.
 *  - fractasia_wolfram_rules: the eight classic Wolfram 1D rules we ship
 *    for the cellular family.
 *  - the palette generator and its quality gate;
 *  - the cellular automaton stepper;
 *  - the view picker for the escape-time families (fract_view_*).
 *
 * Everything here is GL-free so it can be unit-tested without a context. */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fractasia.h"

const char *fract_family_filename(int fid) {
    switch (fid) {
    case FRACT_ESCAPE_TIME:  return "escape_time.frag";
    case FRACT_MULTIBROT:    return "multibrot.frag";
    case FRACT_BURNING_SHIP: return "burning_ship.frag";
    case FRACT_TRICORN:      return "tricorn.frag";
    case FRACT_JULIA:        return "julia.frag";
    case FRACT_QUATERNION:   return "quaternion.frag";
    case FRACT_NEWTON:       return "newton.frag";
    case FRACT_ATTRACTOR:    return "attractor.frag";
    case FRACT_IFS:          return "ifs.frag";
    case FRACT_PLASMA:       return "plasma.frag";
    case FRACT_LSYSTEM:      return "lsystem.frag";
    case FRACT_LYAPUNOV:     return "lyapunov.frag";
    case FRACT_CELLULAR:     return "cellular.frag";
    case FRACT_JULIA_GLOW:   return "julia_glow.frag";
    case FRACT_ORBIT_TRAP:   return "orbit_trap.frag";
    case FRACT_MOBIUS:       return "mobius.frag";
    case FRACT_SHIP_GLOW:    return "ship_glow.frag";
    default:                 return NULL;
    }
}

const int fractasia_wolfram_rules[8] = {
    30,   /* triangle pattern, chaotic */
    90,   /* Sierpinski triangle */
    110,  /* complex local structures */
    184,  /* particle-conserving */
    54,   /* chaotic, similar to rule 30 */
    60,   /* periodic */
    102,  /* Sierpinski-like */
    150   /* chaotic, with periodic gliders */
};

/* ------------------------------------------------------------------- */
/* Palettes                                                             */
/* ------------------------------------------------------------------- */

void fract_palette_eval(const FractPaletteDef *p, float t, float rgb[3], int *clipped) {
    int clip = 0;
    for (int ch = 0; ch < 3; ch++) {
        float v = p->a[ch] + p->b[ch] * cosf(6.2831853f * (p->c[ch] * t + p->d[ch]));
        if (v < 0.0f) { v = 0.0f; clip = 1; }
        else if (v > 1.0f) { v = 1.0f; clip = 1; }
        rgb[ch] = v;
    }
    if (clipped) *clipped = clip;
}

#define PAL_SAMPLES 64
/* The low end of the palette, t in [0, PAL_LOW_T], is what most of the screen
 * shows: escape-time shaders colour far-field pixels (fast escapers) and the set
 * interior with t between 0 and about 0.3. A palette that is bright there
 * washes the whole picture out, which is how the old default came out as pale
 * pink on white even though its full range looked fine. */
#define PAL_LOW_T 0.30f

int fract_palette_acceptable(const FractPaletteDef *p) {
    return fract_palette_acceptable_kind(p, FRACT_PK_COLOUR);
}

int fract_palette_acceptable_kind(const FractPaletteDef *p, int kind) {
    float ymin = 1.0f, ymax = 0.0f, ysum = 0.0f, cmax = 0.0f;
    float low_ysum = 0.0f, low_ymax = 0.0f, low_csum = 0.0f;
    int low_n = 0, clipped = 0;
    for (int i = 0; i < PAL_SAMPLES; i++) {
        float t = (float)i / (float)(PAL_SAMPLES - 1);
        float rgb[3];
        int clip = 0;
        fract_palette_eval(p, t, rgb, &clip);
        clipped += clip;
        float y = 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
        float hi = rgb[0] > rgb[1] ? (rgb[0] > rgb[2] ? rgb[0] : rgb[2]) : (rgb[1] > rgb[2] ? rgb[1] : rgb[2]);
        float lo = rgb[0] < rgb[1] ? (rgb[0] < rgb[2] ? rgb[0] : rgb[2]) : (rgb[1] < rgb[2] ? rgb[1] : rgb[2]);
        float chroma = hi - lo;
        if (y < ymin) ymin = y;
        if (y > ymax) ymax = y;
        ysum += y;
        if (chroma > cmax) cmax = chroma;
        if (t <= PAL_LOW_T) {
            low_n++;
            low_ysum += y;
            low_csum += chroma;
            if (y > low_ymax) low_ymax = y;
        }
    }
    if (low_n == 0) return 0; /* cannot happen at 64 samples; never divide by zero */
    float ymean = ysum / (float)PAL_SAMPLES;
    float low_ymean = low_ysum / (float)low_n;
    float low_cmean = low_csum / (float)low_n;
    /* Thresholds by kind.  COLOUR is the original gate.  MONO and FILM are
     * allowed low chroma (that is the scheme) but must keep the dark low end
     * and the contrast, so an accidental grey still fails on those.  PASTEL
     * may be brighter at the low end but must still carry colour. */
    float low_ymean_max = 0.45f, low_ymax_max = 0.65f, low_cmean_min = 0.10f;
    float ymean_max = 0.62f, cmax_min = 0.45f, contrast_min = 0.40f;
    switch (kind) {
    case FRACT_PK_MONO:   low_cmean_min = 0.0f; cmax_min = 0.08f; break;
    case FRACT_PK_FILM:   low_cmean_min = 0.03f; cmax_min = 0.12f; contrast_min = 0.35f; break;
    case FRACT_PK_PASTEL: low_ymean_max = 0.68f; low_ymax_max = 0.88f; ymean_max = 0.80f; cmax_min = 0.25f; break;
    default: break;
    }
    return
        /* the area-dominant low end: dark-to-mid and not pale, but not black */
        low_ymean >= 0.06f && low_ymean <= low_ymean_max && low_ymax <= low_ymax_max && low_cmean >= low_cmean_min
        /* the whole range still has bright accents, contrast and (per kind) colour */
        && ymax >= 0.55f && (ymax - ymin) >= contrast_min && ymean >= 0.20f && ymean <= ymean_max && cmax >= cmax_min
        /* some clipping is how a palette gets a dark floor and saturated bands;
         * mostly-clipped palettes are flat */
        && clipped <= (int)(0.70f * (float)PAL_SAMPLES);
}

static uint32_t pal_next(uint64_t *s) {
    uint64_t x = *s ? *s : 0x9E3779B97F4A7C15ull;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *s = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1Dull) >> 32);
}
static float pal_rnd(uint64_t *s) { return (float)(pal_next(s) >> 8) * (1.0f / 16777216.0f); }
static float pal_range(uint64_t *s, float lo, float hi) { return lo + (hi - lo) * pal_rnd(s); }

int fract_palette_random(uint64_t *rng, FractPaletteDef *out) {
    return fract_palette_random_scheme(rng, out, 0);
}

int fract_palette_random_scheme(uint64_t *rng, FractPaletteDef *out, int force_cygnus) {
    for (int attempt = 1; attempt <= FRACT_PAL_MAX_ATTEMPTS; attempt++) {
        FractPaletteDef p;
        float hue = pal_rnd(rng);
        float spread1, spread2;
        int cygnus = 0;
        /* Five harmony schemes. The Cygnus scheme (two draws in five) keeps
         * blue high while red and green alternate in antiphase, so the bands
         * run magenta / violet / cyan, the 26.10 release palette; the gate
         * still decides which hue phases are dark enough at the low end. */
        unsigned scheme = pal_next(rng) % 5;
        if (force_cygnus) scheme = 0;
        switch (scheme) {
        case 0:
        case 4:
            cygnus = 1;
            spread1 = pal_range(rng, 0.46f, 0.54f);
            spread2 = pal_range(rng, 0.08f, 0.22f);
            break;
        case 1:  /* analogous: neighbouring hues */
            spread1 = pal_range(rng, 0.06f, 0.18f);
            spread2 = spread1 + pal_range(rng, 0.08f, 0.20f);
            break;
        case 2:  /* complementary: opposite hues with a bridge */
            spread1 = pal_range(rng, 0.44f, 0.56f);
            spread2 = pal_range(rng, 0.18f, 0.32f);
            break;
        case 3:  /* triadic */
            spread1 = pal_range(rng, 0.30f, 0.37f);
            spread2 = pal_range(rng, 0.63f, 0.70f);
            break;
        default: /* free */
            spread1 = pal_rnd(rng);
            spread2 = pal_rnd(rng);
            break;
        }
        float base_a = pal_range(rng, 0.34f, 0.58f);
        float base_b = pal_range(rng, 0.30f, 0.56f);
        float bands = pal_range(rng, 0.6f, 1.7f);
        for (int ch = 0; ch < 3; ch++) {
            p.a[ch] = base_a + pal_range(rng, -0.10f, 0.10f);
            p.b[ch] = base_b + pal_range(rng, -0.08f, 0.08f);
            p.c[ch] = bands * pal_range(rng, 0.85f, 1.15f);
        }
        if (cygnus) {
            /* Blue rides high with a shallow swing; red and green keep the
             * full swing and, through spread1 ~ 0.5, never peak together. */
            p.a[2] = pal_range(rng, 0.52f, 0.66f);
            p.b[2] = pal_range(rng, 0.20f, 0.32f);
            p.a[1] = base_a - pal_range(rng, 0.04f, 0.12f);
        }
        p.d[0] = hue;
        p.d[1] = hue + spread1;
        p.d[2] = hue + spread2;
        if (fract_palette_acceptable(&p)) {
            *out = p;
            return attempt;
        }
    }
    return 0;
}


/* ------------------------------------------------------------------- */
/* Cellular automaton                                                   */
/* ------------------------------------------------------------------- */

void fract_ca_init_row(int rule, uint32_t seed, uint8_t row[FRACT_CA_W]) {
    for (int i = 0; i < FRACT_CA_W; i++) row[i] = 0;
    if (rule == 110 || rule == 184) {
        uint64_t st = 0x9E3779B97F4A7C15ull ^ ((uint64_t)seed * 0xD1B54A32D192ED03ull);
        for (int i = 0; i < FRACT_CA_W; i++) row[i] = (uint8_t)(pal_next(&st) & 1u);
    } else {
        row[FRACT_CA_W / 2] = 1;
    }
}

void fract_ca_step(int rule, const uint8_t in[FRACT_CA_W], uint8_t out[FRACT_CA_W]) {
    for (int i = 0; i < FRACT_CA_W; i++) {
        int l = in[(i + FRACT_CA_W - 1) % FRACT_CA_W];
        int m = in[i];
        int r = in[(i + 1) % FRACT_CA_W];
        out[i] = (uint8_t)((rule >> ((l << 2) | (m << 1) | r)) & 1);
    }
}

int fract_family_in_rotation(int fid) {
    switch (fid) {
    case FRACT_ESCAPE_TIME:
    case FRACT_MULTIBROT:
    case FRACT_BURNING_SHIP:
    case FRACT_TRICORN:
    case FRACT_JULIA:
    case FRACT_PLASMA:
    case FRACT_CELLULAR:
    case FRACT_JULIA_GLOW:
    case FRACT_ORBIT_TRAP:
    case FRACT_MOBIUS:
    case FRACT_SHIP_GLOW:
        return 1;
    default:
        return 0;
    }
}


/* ------------------------------------------------------------------- */
/* Shader preamble                                                      */
/* ------------------------------------------------------------------- */

const char fractasia_frag_preamble[] =
    "#version 320 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "\n"
    "uniform float u_time;\n"
    "uniform vec3  u_resolution;\n"
    "uniform int   u_frame;\n"
    "uniform vec4  u_seed;\n"
    "uniform int   u_iterations;\n"
    "uniform int   u_type;\n"
    "uniform float u_exposure;\n"
    "uniform float u_speed;\n"
    "uniform vec4  u_params;     /* family-specific parameter block */\n"
    "uniform vec4  u_view;       /* escape-time view: (t0, log2 zoom/s, c.re, c.im) */\n"
    "uniform vec4  u_map;        /* (band scale * dir, posterise levels, cycling/s, interior t) */\n"
    "uniform int   u_fx;         /* effect bits, see fractasia.h */\n"
    "uniform vec4  u_fxp;        /* (kaleido sectors, warp amp, grain, aberration) */\n"
    "uniform sampler2D u_palette_a;\n"
    "uniform sampler2D u_palette_b;\n"
    "uniform float     u_palette_blend;\n"
    "uniform float     u_hue;           /* hue drift angle, radians */\n"
    "\n"
    "out vec4 frag_color;\n"
    "\n"
    "#define FX_TRAP_POINT 1\n"
    "#define FX_TRAP_LINE 2\n"
    "#define FX_TRAP_CROSS 4\n"
    "#define FX_STRIPE 8\n"
    "#define FX_DE 16\n"
    "#define FX_EMBOSS 32\n"
    "#define FX_GLOW 64\n"
    "#define FX_CA 128\n"
    "#define FX_VIGNETTE 256\n"
    "#define FX_GRAIN 512\n"
    "#define FX_KALEIDO 1024\n"
    "#define FX_WARP 2048\n"
    "bool fract_fx(int bit) { return (u_fx & bit) != 0; }\n"
    "\n"
    "/* Rotate hue in YIQ space: luma is unchanged, so a dark ground stays dark and\n"
    "   a bright accent stays bright while the colours glide round the wheel. */\n"
    "vec3 fract_hue_rotate(vec3 c, float a) {\n"
    "    const mat3 to_yiq = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);\n"
    "    const mat3 to_rgb = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);\n"
    "    vec3 yiq = to_yiq * c;\n"
    "    float ca = cos(a), sa = sin(a);\n"
    "    yiq.yz = mat2(ca, sa, -sa, ca) * yiq.yz;\n"
    "    return clamp(to_rgb * yiq, 0.0, 1.0);\n"
    "}\n"
    "\n"
    "vec3 fract_palette(float t) {\n"
    "    vec3 a = texture(u_palette_a, vec2(t, 0.5)).rgb;\n"
    "    vec3 b = texture(u_palette_b, vec2(t, 0.5)).rgb;\n"
    "    return fract_hue_rotate(mix(a, b, u_palette_blend), u_hue);\n"
    "}\n"
    "\n"
    "/* fract_finite — NaN/Inf guard. GLSL ES has no isfinite() so we\n"
    "   check (x == x) (NaN) and |x| < 1e30. */\n"
    "bool fract_finite(float x) { return (x == x) && (abs(x) < 1.0e30); }\n"
    "\n"
    "vec3 fract_ramp(float t, float sat) {\n"
    "    return clamp(fract_palette(t) * (0.5 + 0.5 * sat), 0.0, 1.0);\n"
    "}\n"
    "\n"
    "float fract_hash(vec2 p) {\n"
    "    vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
    "    p3 += dot(p3, p3.yzx + 33.33);\n"
    "    return fract((p3.x + p3.y) * p3.z);\n"
    "}\n"
    "\n"
    "/* Screen folds (kaleidoscope, slow domain warp) applied to a centred,\n"
    "   height-normalised coordinate before any family maps it. */\n"
    "vec2 fract_fx_fold(vec2 p) {\n"
    "    if (fract_fx(FX_KALEIDO)) {\n"
    "        float n = max(u_fxp.x, 2.0);\n"
    "        float sector = 6.2831853 / n;\n"
    "        float a = atan(p.y, p.x) + u_time * u_speed * 0.05;\n"
    "        a = abs(mod(a, sector) - 0.5 * sector);\n"
    "        p = length(p) * vec2(cos(a), sin(a));\n"
    "    }\n"
    "    if (fract_fx(FX_WARP)) {\n"
    "        float t = u_time * u_speed * 0.4;\n"
    "        p += u_fxp.y * vec2(sin(p.y * 3.1 + t), cos(p.x * 2.7 - t * 0.8));\n"
    "    }\n"
    "    return p;\n"
    "}\n"
    "\n"
    "/* Pixel size in plane units for the current view (for the distance estimate). */\n"
    "float fract_view_pixel(void) {\n"
    "    float zoom = u_params.z > 0.0 ? u_params.z : 1.0;\n"
    "    zoom = min(zoom * exp2(u_view.y * max(u_time - u_view.x, 0.0)), 600.0);\n"
    "    return 1.5 / (zoom * u_resolution.y);\n"
    "}\n"
    "\n"
    "/* The view of an escape-time family: u_params = (centre.x, centre.y,\n"
    "   zoom at the cycle start, rotation), u_view.x = cycle start time,\n"
    "   u_view.y = log2 zoom per second.  Returns the plane point of a pixel.\n"
    "   The CPU picker (fract_view_probe) runs the same mapping (folds off). */\n"
    "vec2 fract_view_point(vec2 frag) {\n"
    "    vec2 p = (frag - 0.5 * u_resolution.xy) / u_resolution.y;\n"
    "    p = fract_fx_fold(p);\n"
    "    float zoom = u_params.z > 0.0 ? u_params.z : 1.0;\n"
    "    zoom = min(zoom * exp2(u_view.y * max(u_time - u_view.x, 0.0)), 600.0);\n"
    "    float a = u_params.w + u_time * u_speed * 0.02;\n"
    "    float ca = cos(a), sa = sin(a);\n"
    "    p = vec2(ca * p.x - sa * p.y, sa * p.x + ca * p.y) * (1.5 / zoom);\n"
    "    return p + u_params.xy;\n"
    "}\n"
    "\n"
    "/* Orbit statistics the escape-time loops accumulate. */\n"
    "struct FractOrbit {\n"
    "    float nu;      /* smooth escape count, < 0 for the interior */\n"
    "    float trap;    /* min |z|^2 */\n"
    "    float line;    /* min |Im z| */\n"
    "    float cross;   /* min(|Re z|, |Im z|) */\n"
    "    float stripe;  /* sum of sin^2(arg z) */\n"
    "    float n;       /* iterations taken */\n"
    "    float de;      /* distance estimate, or -1 */\n"
    "};\n"
    "FractOrbit fract_orbit_begin(void) {\n"
    "    FractOrbit o; o.nu = -1.0; o.trap = 1.0e9; o.line = 1.0e9; o.cross = 1.0e9;\n"
    "    o.stripe = 0.0; o.n = 0.0; o.de = -1.0; return o;\n"
    "}\n"
    "void fract_orbit_step(inout FractOrbit o, vec2 z, float d2) {\n"
    "    o.trap = min(o.trap, d2);\n"
    "    o.line = min(o.line, abs(z.y));\n"
    "    o.cross = min(o.cross, min(abs(z.x), abs(z.y)));\n"
    "    o.stripe += z.y * z.y / max(d2, 1.0e-12);\n"
    "    o.n += 1.0;\n"
    "}\n"
    "/* Called once on escape: smooth count for degree-n dynamics and, when the\n"
    "   loop tracked dz, the distance estimate |z| ln|z| / |dz|. */\n"
    "void fract_orbit_escape(inout FractOrbit o, int i, float d2, vec2 dz, float inv_log_n) {\n"
    "    o.nu = float(i) + 1.0 - log2(max(0.5 * log2(d2), 1.0e-6)) * inv_log_n;\n"
    "    if (fract_fx(FX_DE)) {\n"
    "        float r = sqrt(d2);\n"
    "        o.de = r * log(r) / max(length(dz), 1.0e-12);\n"
    "    }\n"
    "}\n"
    "\n"
    "/* Band coordinate of an escaped point under the cycle's mapping. */\n"
    "float fract_band(float nu, float phase) {\n"
    "    float l = log2(1.0 + nu);\n"
    "    float b = (l * 0.50 + nu * 0.018) * u_map.x + phase + u_time * u_map.z;\n"
    "    if (u_map.y > 0.5) b = floor(b * u_map.y) / u_map.y;\n"
    "    return abs(fract(b) * 2.0 - 1.0);\n"
    "}\n"
    "vec3 fract_band_colour(float band) {\n"
    "    if (fract_fx(FX_CA)) {\n"
    "        float d = u_fxp.w;\n"
    "        return vec3(fract_palette(clamp(band + d, 0.0, 1.0)).r, fract_palette(band).g,\n"
    "                    fract_palette(clamp(band - d, 0.0, 1.0)).b);\n"
    "    }\n"
    "    return fract_palette(band);\n"
    "}\n"
    "\n"
    "/* Shared escape-time colouring.  Bands are a mirrored sweep of the palette\n"
    "   in log2(1 + nu) with a small linear term, so they survive any zoom\n"
    "   depth; the fastest-escaping far field is held down to a dark ground; the\n"
    "   interior is the hole tinted at the cycle's interior palette position and\n"
    "   lifted by the trap glow.  Effects: orbit traps and stripes modulate the\n"
    "   bands; the distance estimate draws an outline and a glow; emboss shades\n"
    "   the band field by its screen-space gradient. */\n"
    "vec3 fract_escape_shade(FractOrbit o, float phase) {\n"
    "    if (o.nu < 0.0) {\n"
    "        float g = exp(-sqrt(max(o.trap, 0.0)) * 3.0);\n"
    "        /* Interior filaments: only the orbits that pass within a few\n"
    "           hundredths of an axis light up, not the whole hole. */\n"
    "        float tl = fract_fx(FX_TRAP_LINE) || fract_fx(FX_TRAP_CROSS)\n"
    "                   ? exp(-min(o.line, o.cross) * 30.0) : 0.0;\n"
    "        vec3 c = fract_palette(fract(u_map.w + 0.14 * g)) * (0.16 + 0.44 * g);\n"
    "        c += fract_palette(fract(u_map.w + 0.5)) * tl * 0.4;\n"
    "        return c;\n"
    "    }\n"
    "    float band = fract_band(o.nu, phase);\n"
    "    vec3 c = fract_band_colour(band);\n"
    "    float l = log2(1.0 + o.nu);\n"
    "    if (fract_fx(FX_TRAP_POINT) || fract_fx(FX_TRAP_LINE) || fract_fx(FX_TRAP_CROSS)) {\n"
    "        /* The trap distance is the colour coordinate, on a log scale\n"
    "           because orbits pass within 1e-3 of an axis almost everywhere:\n"
    "           variation, not a uniform lift.  The point trap also fades\n"
    "           with distance so it reads as a glow. */\n"
    "        bool pt = fract_fx(FX_TRAP_POINT);\n"
    "        float d = pt ? sqrt(o.trap) : fract_fx(FX_TRAP_LINE) ? o.line : o.cross;\n"
    "        float tw = pt ? exp(-d * 2.5) : 0.6;\n"
    "        vec3 tc = fract_palette(fract(0.15 + log2(1.0 + d * 60.0) * 0.22 + phase * 0.5));\n"
    "        c = mix(c, tc, 0.35 * tw) * (0.9 + 0.3 * tw);\n"
    "    }\n"
    "    if (fract_fx(FX_STRIPE)) {\n"
    "        float s = o.stripe / max(o.n, 1.0);\n"
    "        c *= 0.45 + 1.1 * s;\n"
    "    }\n"
    "    /* The fastest-escaping far field stays a dark ground whatever the\n"
    "       traps and stripes did above. */\n"
    "    c *= mix(0.10, 1.0, smoothstep(0.0, 2.4, l));\n"
    "    if (o.de >= 0.0) {\n"
    "        float px = fract_view_pixel();\n"
    "        float edge = 1.0 - smoothstep(0.0, 1.6 * px, o.de);\n"
    "        c = mix(c, fract_palette(0.95), edge * 0.85);\n"
    "        if (fract_fx(FX_GLOW)) c += fract_palette(0.80) * exp(-o.de / (px * 8.0)) * 0.45;\n"
    "    }\n"
    "    if (fract_fx(FX_EMBOSS)) {\n"
    "        vec2 g = vec2(dFdx(o.nu), dFdy(o.nu));\n"
    "        vec3 nrm = normalize(vec3(-g, 1.5));\n"
    "        float light = max(dot(nrm, normalize(vec3(-0.6, 0.5, 0.6))), 0.0);\n"
    "        float rim = pow(1.0 - nrm.z, 2.0);\n"
    "        c = c * (0.45 + 0.8 * light) + fract_palette(0.9) * rim * 0.6;\n"
    "    }\n"
    "    return c;\n"
    "}\n"
    "/* The pre-effects colouring, for callers that only have nu and the trap. */\n"
    "vec3 fract_escape_colour(float nu, float trap, float phase) {\n"
    "    FractOrbit o = fract_orbit_begin();\n"
    "    o.nu = nu; o.trap = trap; o.n = max(nu, 1.0);\n"
    "    return fract_escape_shade(o, phase);\n"
    "}\n"
    "\n"
    "/* Post effects every family applies last: vignette and film grain. */\n"
    "vec3 fract_fx_post(vec3 c, vec2 frag) {\n"
    "    if (fract_fx(FX_VIGNETTE)) {\n"
    "        vec2 q = (frag - 0.5 * u_resolution.xy) / u_resolution.y;\n"
    "        c *= mix(0.55, 1.0, smoothstep(1.3, 0.35, length(q)));\n"
    "    }\n"
    "    if (fract_fx(FX_GRAIN)) {\n"
    "        float g = fract_hash(frag + fract(u_time * 7.0) * 311.0) - 0.5;\n"
    "        c += g * u_fxp.z * (0.3 + 0.7 * length(c));\n"
    "    }\n"
    "    return c;\n"
    "}\n";

/* ------------------------------------------------------------------- */
/* View picker for the escape-time families                            */
/* ------------------------------------------------------------------- */

int fract_view_family(int fid) {
    switch (fid) {
    case FRACT_ESCAPE_TIME:
    case FRACT_MULTIBROT:
    case FRACT_BURNING_SHIP:
    case FRACT_TRICORN:
    case FRACT_JULIA:
        return 1;
    default:
        return 0;
    }
}

/* Inside the Mandelbrot set's main cardioid or period-2 bulb. */
static int mandel_interior(float x, float y) {
    float xq = x - 0.25f, q = xq * xq + y * y;
    if (q * (q + xq) <= 0.25f * y * y) return 1;
    float xb = x + 1.0f;
    return xb * xb + y * y <= 0.0625f;
}

/* z <- z^n + c, |z| > 2 bailout; the one recurrence multibrot's c search and
 * its Julia view share. */
static int pow_iterate(int n, float zx, float zy, float cx, float cy, int max_iter) {
    for (int i = 0; i < max_iter; i++) {
        float rx = 1.0f, ry = 0.0f, bx = zx, by = zy;
        for (int e = n; e > 0; e >>= 1) {
            if (e & 1) {
                float tx = rx * bx - ry * by;
                ry = rx * by + ry * bx;
                rx = tx;
            }
            float sx = bx * bx - by * by;
            by = 2.0f * bx * by;
            bx = sx;
        }
        zx = rx + cx; zy = ry + cy;
        if (zx * zx + zy * zy > 4.0f) return i;
    }
    return max_iter;
}

int fract_view_iterate(int fid, const FractView *v, float x, float y, int max_iter) {
    float zx = x, zy = y;
    float cx = x, cy = y;
    int n = v->n < 3 ? 3 : (v->n > 8 ? 8 : v->n);
    if (fid == FRACT_JULIA || fid == FRACT_MULTIBROT) { cx = v->cre; cy = v->cim; }
    else if (fid == FRACT_ESCAPE_TIME && mandel_interior(x, y)) return max_iter;
    if (fid == FRACT_MULTIBROT) return pow_iterate(n, zx, zy, cx, cy, max_iter);
    for (int i = 0; i < max_iter; i++) {
        float nx, ny;
        switch (fid) {
        case FRACT_TRICORN:
            nx = zx * zx - zy * zy + cx;
            ny = -2.0f * zx * zy + cy;
            break;
        case FRACT_BURNING_SHIP: {
            float ax = fabsf(zx), ay = fabsf(zy);
            nx = ax * ax - ay * ay + cx;
            ny = 2.0f * ax * ay + cy;
            break;
        }
        default: /* escape-time and julia: z^2 + c */
            nx = zx * zx - zy * zy + cx;
            ny = 2.0f * zx * zy + cy;
            break;
        }
        zx = nx; zy = ny;
        if (zx * zx + zy * zy > 4.0f) return i;
    }
    return max_iter;
}

/* Plane point of probe cell (i, j) under the view, the shader's mapping at
 * the cycle start. */
static void view_cell_point(const FractView *v, int i, int j, float *x, float *y) {
    float px = ((float)i + 0.5f) / (float)FRACT_PROBE_W - 0.5f;
    float py = ((float)j + 0.5f) / (float)FRACT_PROBE_H - 0.5f;
    px *= (float)FRACT_PROBE_W / (float)FRACT_PROBE_H;  /* 16:9 like the screen */
    float ca = cosf(v->rot), sa = sinf(v->rot);
    float s = 1.5f / (v->zoom > 0.0f ? v->zoom : 1.0f);
    *x = (ca * px - sa * py) * s + v->cx;
    *y = (sa * px + ca * py) * s + v->cy;
}

static int probe_budget(int max_iter) {
    if (max_iter < FRACT_PROBE_ITER_MIN) return FRACT_PROBE_ITER_MIN;
    if (max_iter > FRACT_PROBE_ITER) return FRACT_PROBE_ITER;
    return max_iter;
}

static void view_probe_grid(int fid, const FractView *v, int max_iter, int it[FRACT_PROBE_H][FRACT_PROBE_W]) {
    for (int j = 0; j < FRACT_PROBE_H; j++)
        for (int i = 0; i < FRACT_PROBE_W; i++) {
            float x, y;
            view_cell_point(v, i, j, &x, &y);
            it[j][i] = fract_view_iterate(fid, v, x, y, max_iter);
        }
}

/* Two neighbouring cells straddle structure when one is interior and the
 * other is not, or when both are exterior past the far field and their
 * counts differ by a quarter or more. */
static int view_edge(int a, int b, int max_iter) {
    int ia = a >= max_iter, ib = b >= max_iter;
    if (ia != ib) return 1;
    if (ia) return 0;
    int lo = a < b ? a : b, d = a < b ? b - a : a - b;
    return lo >= 6 && d >= (lo / 4 > 2 ? lo / 4 : 2);
}

static void view_stats(const int it[FRACT_PROBE_H][FRACT_PROBE_W], int max_iter,
                       unsigned char bnd[FRACT_PROBE_H][FRACT_PROBE_W], FractViewStats *st) {
    int n = FRACT_PROBE_W * FRACT_PROBE_H, interior = 0, far = 0, boundary = 0, ext = 0;
    int central = 0, ncentral = 0;
    const int ci0 = FRACT_PROBE_W / 5, ci1 = FRACT_PROBE_W - FRACT_PROBE_W / 5;
    const int cj0 = FRACT_PROBE_H / 5, cj1 = FRACT_PROBE_H - FRACT_PROBE_H / 5;
    double s1 = 0.0, s2 = 0.0;
    for (int j = 0; j < FRACT_PROBE_H; j++)
        for (int i = 0; i < FRACT_PROBE_W; i++) {
            int c = it[j][i];
            if (c >= max_iter) interior++;
            else {
                if (c < 3) far++;
                double l = log2(1.0 + (double)c);
                s1 += l; s2 += l * l; ext++;
            }
            int b = (i > 0 && view_edge(c, it[j][i - 1], max_iter)) ||
                    (i + 1 < FRACT_PROBE_W && view_edge(c, it[j][i + 1], max_iter)) ||
                    (j > 0 && view_edge(c, it[j - 1][i], max_iter)) ||
                    (j + 1 < FRACT_PROBE_H && view_edge(c, it[j + 1][i], max_iter));
            if (bnd) bnd[j][i] = (unsigned char)b;
            boundary += b;
            if (i >= ci0 && i < ci1 && j >= cj0 && j < cj1) { ncentral++; central += b; }
        }
    st->central = ncentral ? (float)central / (float)ncentral : 0.0f;
    st->interior = (float)interior / (float)n;
    st->far = (float)far / (float)n;
    st->boundary = (float)boundary / (float)n;
    if (ext > 1) {
        double mean = s1 / ext, var = s2 / ext - mean * mean;
        st->spread = (float)sqrt(var > 0.0 ? var : 0.0);
    } else st->spread = 0.0f;
}

void fract_view_probe(int fid, const FractView *v, int max_iter, FractViewStats *st) {
    static int it[FRACT_PROBE_H][FRACT_PROBE_W];
    int m = probe_budget(max_iter);
    view_probe_grid(fid, v, m, it);
    view_stats(it, m, NULL, st);
}

int fract_view_acceptable(const FractViewStats *st) {
    return st->boundary >= 0.10f                 /* structure spread over the frame, not a hairline */
        && st->central >= 0.07f                  /* and some of it in the middle, not all in a corner */
        && st->interior <= 0.70f                 /* not a screen of hole */
        && st->far <= 0.60f                      /* not a screen of flat far field */
        && st->far + st->interior <= 0.70f       /* at least 30% working exterior: the old
                                                    whole-set window (37% hole + 36% far) fails */
        && st->spread >= 0.45f;                  /* real variety in the escape counts */
}

float fract_view_zoom_at(const FractView *v, double t) {
    double z = (double)(v->zoom > 0.0f ? v->zoom : 1.0f) * exp2((double)v->zoom_rate * (t > 0.0 ? t : 0.0));
    return z > FRACT_VIEW_MAX_ZOOM ? FRACT_VIEW_MAX_ZOOM : (float)z;
}

/* The default window of each family: where the set sits at zoom 1. */
static void view_base(int fid, FractView *v) {
    memset(v, 0, sizeof *v);
    v->n = 2;
    v->zoom = 1.0f;
    switch (fid) {
    case FRACT_ESCAPE_TIME:  v->cx = -0.60f; v->cy =  0.00f; v->zoom = 0.85f; break;
    case FRACT_MULTIBROT:    v->cx =  0.00f; v->cy =  0.00f; v->zoom = 0.80f; v->n = 3; v->cre = 0.36f; v->cim = 0.10f; break;
    case FRACT_BURNING_SHIP: v->cx = -0.45f; v->cy = -0.45f; v->zoom = 0.75f; break;
    case FRACT_TRICORN:      v->cx = -0.25f; v->cy =  0.00f; v->zoom = 0.80f; break;
    case FRACT_JULIA:        v->cx =  0.00f; v->cy =  0.00f; v->zoom = 0.80f; v->cre = -0.4f; v->cim = 0.6f; break;
    default: break;
    }
}

/* Curated fallbacks, used only when every random draw fails the gate. */
static void view_fallback(int fid, int n, FractView *v) {
    view_base(fid, v);
    v->zoom_rate = 0.03f;
    switch (fid) {
    case FRACT_ESCAPE_TIME:  v->cx = -0.7450f; v->cy = 0.1130f; v->zoom = 12.0f; break; /* seahorse valley */
    case FRACT_MULTIBROT: {
        /* c just inside the set's real-axis tip, c* = (n-1)/n * n^(-1/(n-1)). */
        int k = n < 3 ? 3 : (n > 8 ? 8 : n);
        float tip = ((float)k - 1.0f) / (float)k * powf((float)k, -1.0f / ((float)k - 1.0f));
        v->n = k; v->cre = 0.97f * tip; v->cim = 0.02f; v->zoom = 1.0f;
        break;
    }
    case FRACT_BURNING_SHIP: v->cx = -1.7550f; v->cy = -0.0300f; v->zoom = 14.0f; break; /* the small ship */
    case FRACT_TRICORN:      v->cx = -0.1300f; v->cy = 0.9900f; v->zoom = 6.0f; break;
    case FRACT_JULIA:        v->cre = -0.7450f; v->cim = 0.1130f; v->zoom = 1.0f; break;
    default: break;
    }
}

/* Escape count of c in the parameter space of z^n + c (z0 = c). */
static int param_iterate(int n, float cx, float cy, int max_iter) {
    FractView m; memset(&m, 0, sizeof m); m.n = 2;
    if (n <= 2) return fract_view_iterate(FRACT_ESCAPE_TIME, &m, cx, cy, max_iter);
    return pow_iterate(n, cx, cy, cx, cy, max_iter);
}

/* A Julia c within ~0.006 of the boundary of the Mandelbrot (n = 2) or
 * multibrot set: the Julia sets there are connected (or barely not) and
 * carry the richest structure.  Falls back to a classic c. */
static void view_pick_julia_c(uint64_t *rng, int n, FractView *v) {
    float x0 = n <= 2 ? -1.85f : -1.25f, x1 = n <= 2 ? 0.55f : 1.25f, y1 = n <= 2 ? 1.15f : 1.25f;
    for (int s = 0; s < 96; s++) {
        float cx = pal_range(rng, x0, x1), cy = pal_range(rng, -y1, y1);
        int c0 = param_iterate(n, cx, cy, 200);
        if (c0 < 18) continue;              /* far field */
        int in = c0 >= 200, other = 0;
        const float d = 0.006f;
        const float ox[4] = { d, -d, 0.0f, 0.0f }, oy[4] = { 0.0f, 0.0f, d, -d };
        for (int k = 0; k < 4 && !other; k++)
            other = (param_iterate(n, cx + ox[k], cy + oy[k], 200) >= 200) != in;
        if (other) { v->cre = cx; v->cim = cy; return; }
    }
    if (n <= 2) {
        float th = pal_rnd(rng) * 6.2831853f;
        v->cre = 0.7885f * cosf(th);
        v->cim = 0.7885f * sinf(th);
    } else {
        FractView f; view_fallback(FRACT_MULTIBROT, n, &f);
        v->cre = f.cre; v->cim = f.cim;
    }
}

/* Weight of a boundary cell in the draw: exterior cells with slow escapes
 * (filaments, valleys) count for more than the smooth rim of a bulb or an
 * interior cell, so the next zoom lands on structure rather than on hole. */
static int view_cell_weight(int it, int max_iter) {
    if (it >= max_iter) return 2;
    return 2 + (it < 48 ? it : 48);
}

/* Choose a random boundary cell of the probed view (weighted as above),
 * restricted to the central region when `central` is set, and return 1
 * with its plane point. */
static int view_random_boundary(uint64_t *rng, const FractView *v,
                                const int it[FRACT_PROBE_H][FRACT_PROBE_W], int max_iter,
                                unsigned char bnd[FRACT_PROBE_H][FRACT_PROBE_W],
                                int central, float *x, float *y) {
    long total = 0;
    int i0 = central ? FRACT_PROBE_W / 5 : 0, i1 = central ? FRACT_PROBE_W - FRACT_PROBE_W / 5 : FRACT_PROBE_W;
    int j0 = central ? FRACT_PROBE_H / 5 : 0, j1 = central ? FRACT_PROBE_H - FRACT_PROBE_H / 5 : FRACT_PROBE_H;
    for (int j = j0; j < j1; j++) for (int i = i0; i < i1; i++) if (bnd[j][i]) total += view_cell_weight(it[j][i], max_iter);
    if (!total) return 0;
    long pick = (long)(pal_next(rng) % (uint32_t)total);
    for (int j = j0; j < j1; j++)
        for (int i = i0; i < i1; i++)
            if (bnd[j][i]) {
                pick -= view_cell_weight(it[j][i], max_iter);
                if (pick < 0) { view_cell_point(v, i, j, x, y); return 1; }
            }
    return 0;
}

/* Centroid of the central boundary cells of a probed view: where a view
 * that failed the gate on too much hole or far field should look instead. */
static int view_boundary_centroid(const FractView *v, unsigned char bnd[FRACT_PROBE_H][FRACT_PROBE_W],
                                  float *x, float *y) {
    double sx = 0.0, sy = 0.0; int n = 0;
    for (int j = FRACT_PROBE_H / 6; j < FRACT_PROBE_H - FRACT_PROBE_H / 6; j++)
        for (int i = FRACT_PROBE_W / 6; i < FRACT_PROBE_W - FRACT_PROBE_W / 6; i++)
            if (bnd[j][i]) { float px, py; view_cell_point(v, i, j, &px, &py); sx += px; sy += py; n++; }
    if (n < 4) return 0;
    *x = (float)(sx / n); *y = (float)(sy / n);
    return 1;
}

int fract_view_pick(uint64_t *rng, int fid, int n, int max_iter, FractView *out) {
    static int it[FRACT_PROBE_H][FRACT_PROBE_W];
    static unsigned char bnd[FRACT_PROBE_H][FRACT_PROBE_W];
    FractViewStats st;
    int m = probe_budget(max_iter);
    /* Level 0 for the parameter-space families: probe the default window
     * once per pick (its boundary cells do not depend on the attempt), then
     * draw a different cell from it each attempt.  The Julia families
     * re-probe because their c, and so their set, changes every attempt. */
    FractView base;
    view_base(fid, &base);
    int julia = fid == FRACT_JULIA || fid == FRACT_MULTIBROT;
    if (fid == FRACT_MULTIBROT) base.n = n < 3 ? 3 : (n > 8 ? 8 : n);
    if (!julia) {
        view_probe_grid(fid, &base, m, it);
        view_stats(it, m, bnd, &st);
    }
    for (int attempt = 1; attempt <= FRACT_VIEW_MAX_ATTEMPTS; attempt++) {
        FractView v = base;
        if (julia) {
            view_pick_julia_c(rng, v.n, &v);
            view_probe_grid(fid, &v, m, it);
            view_stats(it, m, bnd, &st);
        }
        float x, y;
        if (!view_random_boundary(rng, &v, it, m, bnd, 0, &x, &y)) continue;
        v.rot = pal_rnd(rng) * 6.2831853f;
        v.cx = x; v.cy = y;
        v.zoom *= exp2f(pal_range(rng, 0.8f, 3.0f));          /* x1.7 .. x8 */

        /* Level 1 (three draws in five): zoom again onto a boundary cell near
         * the middle of that view, so the cycle starts deeper in. */
        if (pal_next(rng) % 5 < 3) {
            view_probe_grid(fid, &v, m, it);
            view_stats(it, m, bnd, &st);
            if (view_random_boundary(rng, &v, it, m, bnd, 1, &x, &y)) {
                v.cx = x; v.cy = y;
                v.zoom *= exp2f(pal_range(rng, 0.5f, 2.0f));  /* x1.4 .. x4 */
            }
        }

        /* The cycle keeps zooming in: x2 .. x6 per minute. */
        v.zoom_rate = log2f(pal_range(rng, 2.0f, 6.0f)) / 60.0f;

        view_probe_grid(fid, &v, m, it);
        view_stats(it, m, bnd, &st);
        if (fract_view_acceptable(&st)) { *out = v; return attempt; }
        /* One correction: look at the middle of whatever structure the view
         * did show, and judge again. */
        if (st.boundary >= 0.05f && view_boundary_centroid(&v, bnd, &x, &y)) {
            v.cx = x; v.cy = y;
            fract_view_probe(fid, &v, m, &st);
            if (fract_view_acceptable(&st)) { *out = v; return attempt; }
        }
    }
    view_fallback(fid, n, out);
    return 0;
}


/* ------------------------------------------------------------------- */
/* Palette library                                                      */
/* ------------------------------------------------------------------- */

/* col(t) = a + b cos(2 pi (c t + d)) per channel.  Each set is tuned so the
 * low end (t <= 0.3) is dark and the range carries its character; the
 * palette unit test runs every entry through its kind's gate. */
const FractPaletteLib fractasia_palette_library[FRACT_LIB_COUNT] = {
    {"fire",      {{0.42f, 0.14f, 0.03f}, {0.52f, 0.34f, 0.10f}, {1.00f, 1.00f, 0.80f}, {0.00f, 0.12f, 0.30f}}, FRACT_PK_COLOUR, 0},
    {"ice",       {{0.18f, 0.36f, 0.52f}, {0.28f, 0.34f, 0.36f}, {1.00f, 1.00f, 1.00f}, {0.55f, 0.48f, 0.40f}}, FRACT_PK_COLOUR, 0},
    {"aurora",    {{0.12f, 0.40f, 0.34f}, {0.30f, 0.44f, 0.46f}, {1.00f, 1.00f, 0.70f}, {0.40f, 0.15f, 0.65f}}, FRACT_PK_COLOUR, 0},
    {"sunset",    {{0.48f, 0.24f, 0.30f}, {0.46f, 0.34f, 0.40f}, {1.00f, 1.00f, 0.60f}, {0.00f, 0.15f, 0.35f}}, FRACT_PK_COLOUR, 0},
    {"ocean",     {{0.10f, 0.32f, 0.46f}, {0.28f, 0.42f, 0.46f}, {1.00f, 1.00f, 1.00f}, {0.60f, 0.42f, 0.30f}}, FRACT_PK_COLOUR, 0},
    {"synthwave", {{0.38f, 0.33f, 0.60f}, {0.56f, 0.58f, 0.24f}, {1.20f, 1.45f, 1.39f}, {0.52f, 0.01f, 0.69f}}, FRACT_PK_COLOUR, 1},
    {"mono",      {{0.26f, 0.28f, 0.34f}, {0.30f, 0.30f, 0.32f}, {1.00f, 1.00f, 1.00f}, {0.08f, 0.08f, 0.14f}}, FRACT_PK_MONO,   0},
    {"pastel",    {{0.50f, 0.46f, 0.54f}, {0.42f, 0.42f, 0.42f}, {1.00f, 1.00f, 1.00f}, {0.00f, 0.18f, 0.36f}}, FRACT_PK_PASTEL, 0},
    {"oilslick",  {{0.40f, 0.40f, 0.42f}, {0.48f, 0.48f, 0.48f}, {2.00f, 1.50f, 1.00f}, {0.00f, 0.20f, 0.50f}}, FRACT_PK_COLOUR, 0},
    {"gold",      {{0.40f, 0.28f, 0.10f}, {0.46f, 0.34f, 0.20f}, {1.00f, 1.00f, 1.00f}, {0.05f, 0.12f, 0.25f}}, FRACT_PK_COLOUR, 0},
    {"nebula",    {{0.32f, 0.24f, 0.46f}, {0.46f, 0.42f, 0.46f}, {1.00f, 1.30f, 0.80f}, {0.75f, 0.50f, 0.20f}}, FRACT_PK_COLOUR, 1},
    {"acid",      {{0.34f, 0.52f, 0.16f}, {0.48f, 0.40f, 0.48f}, {1.00f, 1.00f, 1.00f}, {0.25f, 0.05f, 0.60f}}, FRACT_PK_COLOUR, 0},
    {"film",      {{0.31f, 0.29f, 0.26f}, {0.36f, 0.32f, 0.28f}, {1.00f, 1.00f, 1.00f}, {0.50f, 0.48f, 0.46f}}, FRACT_PK_FILM,  0},
};

const FractPaletteLib *fract_palette_library_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < FRACT_LIB_COUNT; i++)
        if (!strcmp(fractasia_palette_library[i].name, name)) return &fractasia_palette_library[i];
    return NULL;
}

int fract_palette_cycle(uint64_t *rng, FractPaletteDef *out, const char **name) {
    unsigned r = pal_next(rng) % 10;
    if (r < 4) {
        const FractPaletteLib *l = &fractasia_palette_library[pal_next(rng) % FRACT_LIB_COUNT];
        *out = l->def;
        if (name) *name = l->name;
        return l->cygnus;
    }
    int force = r < 6;
    if (!fract_palette_random_scheme(rng, out, force)) {
        /* No draw passed the gate: a library palette instead. */
        const FractPaletteLib *l = &fractasia_palette_library[pal_next(rng) % FRACT_LIB_COUNT];
        *out = l->def;
        if (name) *name = l->name;
        return l->cygnus;
    }
    if (name) *name = force ? "cygnus" : "generated";
    return force;
}

/* ------------------------------------------------------------------- */
/* Mapping, recipes, effect parameters                                 */
/* ------------------------------------------------------------------- */

void fract_mapping_draw(uint64_t *rng, FractMapping *out) {
    static const float scales[5] = { 0.6f, 0.8f, 1.0f, 1.3f, 1.7f };
    out->band_scale = scales[pal_next(rng) % 5];
    out->posterise = (pal_next(rng) % 10 < 3) ? 4 + (int)(pal_next(rng) % 6) : 0;
    out->direction = (pal_next(rng) & 1) ? 1.0f : -1.0f;
    out->cycle = (pal_next(rng) % 10 < 4) ? 0.0f : pal_range(rng, 0.010f, 0.040f);
    out->interior_t = pal_rnd(rng);
}

const FractRecipe fractasia_recipes[FRACT_RECIPE_COUNT] = {
    /* weak-safe: no derivative tracking in the loop */
    {"plain",        0,                                                            0},
    {"neon-edge",    FRACT_FX_EMBOSS | FRACT_FX_VIGNETTE,                          0},
    {"trap-point",   FRACT_FX_TRAP_POINT | FRACT_FX_VIGNETTE,                      0},
    {"trap-cross",   FRACT_FX_TRAP_CROSS | FRACT_FX_GRAIN,                         0},
    {"stripes",      FRACT_FX_STRIPE | FRACT_FX_VIGNETTE,                          0},
    {"kaleido",      FRACT_FX_KALEIDO | FRACT_FX_VIGNETTE,                         0},
    {"warp-drift",   FRACT_FX_WARP | FRACT_FX_GRAIN,                               0},
    {"prism",        FRACT_FX_CA | FRACT_FX_VIGNETTE | FRACT_FX_GRAIN,             0},
    {"film",         FRACT_FX_GRAIN | FRACT_FX_VIGNETTE | FRACT_FX_TRAP_LINE,      0},
    /* medium and up: the distance estimate doubles the loop's arithmetic */
    {"outline",      FRACT_FX_DE | FRACT_FX_VIGNETTE,                              1},
    {"glow",         FRACT_FX_DE | FRACT_FX_GLOW | FRACT_FX_VIGNETTE,              1},
    {"trap-glow",    FRACT_FX_TRAP_LINE | FRACT_FX_DE | FRACT_FX_GLOW,             1},
    /* high: two heavy passes together */
    {"kaleido-glow", FRACT_FX_KALEIDO | FRACT_FX_DE | FRACT_FX_GLOW | FRACT_FX_CA, 2},
    {"emboss-de",    FRACT_FX_DE | FRACT_FX_EMBOSS | FRACT_FX_TRAP_POINT,          2},
};

const FractRecipe *fract_recipe_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < FRACT_RECIPE_COUNT; i++)
        if (!strcmp(fractasia_recipes[i].name, name)) return &fractasia_recipes[i];
    return NULL;
}

int fract_recipe_draw(uint64_t *rng, int quality, int all) {
    int allowed[FRACT_RECIPE_COUNT], n = 0;
    for (int i = 0; i < FRACT_RECIPE_COUNT; i++)
        if (all || fractasia_recipes[i].min_quality <= quality) allowed[n++] = i;
    if (!n) return 0;
    return allowed[pal_next(rng) % (uint32_t)n];
}

void fract_fx_params_draw(uint64_t *rng, float out[4]) {
    out[0] = (float)(3 + (int)(pal_next(rng) % 6));   /* kaleidoscope sectors 3..8 */
    out[1] = pal_range(rng, 0.02f, 0.08f);            /* warp amplitude, window units */
    out[2] = pal_range(rng, 0.02f, 0.05f);            /* grain amount */
    out[3] = pal_range(rng, 0.010f, 0.030f);          /* aberration, palette units */
}
