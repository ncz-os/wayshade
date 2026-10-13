/* SPDX-License-Identifier: Apache-2.0 */
/* xst_core.h - the generic option, seed, palette and director mechanism of the
 * xshadertoy family ("xshadertoy_opts").
 *
 * A shader opts in by carrying //@ directives in its source; everything else
 * (option table, environment, config file, presets, schema dump, randomize,
 * palette, director, logging) is generic. See docs/XSHADERTOY-OPTIONS.md.
 *
 *   //@saver id=physarum version=2 family=dark|ink director=90
 *   //@palette-native bg=#0e1526 body=#e9dcc6 accent=#c7a27a accent2=#7f93b5
 *   //@option name=filament-density type=float default=0.5 min=0 max=1
 *   //@+      rand=0.35:0.7 group=Simulation label="Filament density"
 *   //@+      desc="How much of the frame the network covers."
 *
 * "//@+" continues the previous directive. Shaders without //@saver behave
 * exactly as before: no options, no extra uniforms, no defines.
 *
 * This file has no GL dependency so the unit tests and the headless render
 * tool can use it.
 */
#ifndef XST_CORE_H
#define XST_CORE_H

#include <stddef.h>
#include <stdint.h>
#include "ncz_options.h"

#define XST_MAX_OPTS   48
#define XST_MAX_BINDS  8
#define XST_POOL       24576
#define XST_DIR_PARAMS 8   /* slow parameters, uDir[0..1] */
#define XST_DIR_PHASES 8   /* integrated phases, uDir[2..3] */

typedef struct {
    int    has_saver;              /* 0: legacy shader, nothing is injected */
    char   id[64];                 /* saver id, e.g. "caustics-on-stone" */
    int    version;
    char   family[128];            /* palette tags, '|' separated */
    double director_period;        /* seconds between director keyframes */
    float  native[4][3];           /* native palette, sRGB 0..1 */
    int    has_native;
    ncz_opt_def defs[XST_MAX_OPTS];
    size_t n;
    /* randomize ranges, parallel to defs */
    int    has_rand[XST_MAX_OPTS];
    double rlo[XST_MAX_OPTS], rhi[XST_MAX_OPTS];
    const char *rchoices[XST_MAX_OPTS]; /* enum: '|' separated subset */
    /* uniform binding, parallel to defs: "" = #define */
    char   bind_name[XST_MAX_OPTS][32];
    int    bind_comp[XST_MAX_OPTS];
    int    shader_opt[XST_MAX_OPTS]; /* 1 for options declared by the shader */
    char   prefix[128];            /* NCZ_XSHADERTOY_<ID>_ */
    char   group[96];              /* xshadertoy_<id> */
    char   errors[512];            /* parse problems (also printed) */
    char   pool[XST_POOL];
    size_t pool_used;
} XstSpec;

typedef struct {
    const XstSpec *spec;
    uint32_t seed;                 /* effective run seed, 1..999999 */
    int      seed_was_random;
    char     val[XST_MAX_OPTS][NCZ_OPT_VALMAX]; /* effective values */
    unsigned char randomized[XST_MAX_OPTS];
    int      quality;              /* 0 weak, 1 mid, 2 strong */
    float    speed, evolution;
    float    iseed[4];             /* geometry stream */
    int      palette_index;        /* -1 native */
    int      palette_next;         /* cycling: next index, -1 none */
    double   palette_cycle_s;      /* 0 = no cycling */
    float    pal_lin[4][3];        /* current palette, linear */
    /* bound uniforms */
    int      nbind;
    char     bind_uniform[XST_MAX_BINDS][32];
    float    bind_val[XST_MAX_BINDS][4];
    /* director cache (pure speed-up; values do not depend on it) */
    long     dir_cache_seg[XST_DIR_PHASES];
    double   dir_cache_sum[XST_DIR_PHASES];
} XstRun;

/* Parse //@ directives. basename is the shader file name ("physarum.glsl"),
 * used for the id when //@saver has none. Returns 0 (also for a legacy
 * shader: has_saver = 0), -1 on a fatal error (spec->errors says why). */
int  xst_spec_parse(XstSpec *spec, const char *basename, const char *src);

/* Start a run from resolved options (o may be NULL: defaults). quality_hint is
 * the GPU-class quality 0..2 used when the quality option is auto. */
void xst_run_init(XstRun *run, const XstSpec *spec, const ncz_opts *o, int quality_hint);

/* Effective value of an option in this run ("" if unknown). */
const char *xst_run_get(const XstRun *run, const char *name);

/* One line describing the run (seed, preset, palette, quality, options). */
void xst_run_describe(const XstRun *run, char *out, size_t sz, int w, int h,
                      uint32_t src_hash);

/* Text inserted right after "#version": NCZ_QUALITY, OPT_* defines, and the
 * xshadertoy_opts uniforms. Caller frees. */
char *xst_defines(const XstRun *run);

/* Splice #include "file" lines (one level, from the given directories, NULL
 * terminated). Caller frees. Returns NULL on a missing include. */
char *xst_splice_includes(const char *body, const char *const *dirs);

/* Director and palette at time t (seconds of motion time, i.e. already
 * multiplied by speed). out_dir receives 16 floats (uDir[4]); out_pal 12
 * floats (uPal[4], linear RGB). Pure functions of (seed, options, t). */
void xst_director(XstRun *run, double t, float out_dir[16]);
void xst_palette_at(const XstRun *run, double t, float out_pal[12]);

/* helpers shared with the tests */
uint32_t xst_hash_u32(uint32_t x);
uint32_t xst_stream(uint32_t seed, const char *key);
double   xst_quintic_integral(double u); /* integral of the quintic 0..u */
uint32_t xst_fnv1a(const char *s);

/* Source assembly shared by the driver and the headless tool. */
char *xst_fragment_source(const XstRun *run, const char *body); /* caller frees */
const char *const *xst_search_dirs(void);
char *xst_load_shader(const char *name, char *used, size_t cap);   /* caller frees */
char *xst_splice_near(const char *body, const char *used_path);    /* caller frees */
#endif
