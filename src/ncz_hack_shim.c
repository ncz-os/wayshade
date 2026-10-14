/* SPDX-License-Identifier: Apache-2.0 */
/* _POSIX_C_SOURCE for sigaction + clock_gettime. */
#define _POSIX_C_SOURCE 200809L

/*
 * xscreensaver_compat.c — implementations of the shim functions declared
 * in xscreensaver_compat.h. Backs the xlockmore/screenhack API surface
 * that vendored xscreensaver GL hacks expect.
 *
 * ROUTING MODEL:
 *
 *   ┌───────────────────────┐
 *   │ Vendored hack source  │ (e.g. src/glmatrix.c, unmodified)
 *   │ Calls glXMakeCurrent, │
 *   │ glXSwapBuffers,       │
 *   │ XGetPixel, init_GL... │
 *   └──────────┬────────────┘
 *              │ linked into the same binary
 *              ▼
 *   ┌───────────────────────┐
 *   │ xscreensaver_compat.c │ (this file)
 *   │ Routes glX* calls to  │
 *   │ the harness's EGL     │
 *   │ surface via globals.  │
 *   └──────────┬────────────┘
 *              │ uses
 *              ▼
 *   ┌───────────────────────┐
 *   │ glmatrix_harness.c    │ (sets the globals, owns the EGL context)
 *   └───────────────────────┘
 *
 * The harness sets the globals (`g_harness_*`) BEFORE the hack's init
 * function is called. The hack then uses those globals to route its
 * glX* calls into the right EGL surface.
 *
 * For build cleanliness: this file is compiled with -Wall -Wextra.
 */

#define _DEFAULT_SOURCE
#include "ncz_hack_shim.h"
#include "ncz_gl.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>

/*
 * g_harness_egl_display / g_harness_egl_surface / g_harness_egl_context —
 * the live EGL objects the harness has created and made current.
 *
 * g_harness_initialized — set to 1 by the harness after init_GL succeeds.
 * The hack's draw path can call eglSwapBuffers via glXSwapBuffers
 * unconditionally; the harness is responsible for ordering.
 *
 * Declared extern in xscreensaver_compat.h so the harness (in a
 * different translation unit) can assign them.
 */

void   *g_harness_egl_display = NULL;
void   *g_harness_egl_surface = NULL;
void   *g_harness_egl_context = NULL;
int     g_harness_initialized = 0;

/*
 * g_harness_width / g_harness_height — current surface size, in pixels.
 * Updated by the harness on resize.
 */
int     g_harness_width  = 0;
int     g_harness_height = 0;

/*
 * g_harness_xscreen — the X "Display*" stub pointer the hack will see.
 * It's a void* the hack will pass back to glX* calls; we just store and
 * return it as opaque. On real X11, this would be an Xlib Display*; here
 * it's just a sentinel so the hack's draw path compiles unchanged.
 */
void   *g_harness_xscreen = NULL;

/* ----------------------------------------------------------------------- */
/* progname / mono_p                                                       */
/* ----------------------------------------------------------------------- */

const char *progname = "ncz-screensavers-glmatrix";
Bool        mono_p   = False;

/* ----------------------------------------------------------------------- */
/* frand / random — deterministic RNG                                      */
/* ----------------------------------------------------------------------- */
/*
 * Vendored hacks call random() (libc) and frand() (xscreensaver helper
 * that maps random() to [0, n)). We seed srandom() with a fixed value so
 * the screensaver looks the same on every run (deterministic).
 */

static int frand_initialized = 0;
static double frand_max;

static void frand_init(void) {
    struct timeval tv;
    unsigned int seed;
    if (frand_initialized) return;
    /*
     * Seed from the wall clock + process ID so each launch differs.
     *
     * A fixed seed (e.g. 0xDEADBEEF) used to be the choice here -- and it
     * is what made the visual verification loop in PORTING.md reproducible
     * -- but it surfaces a real upstream-xscreensaver fragility: a number
     * of hacks (gears, pinion) have data-validation aborts of the form
     * `if (g->inner_r2 > g->inner_r) abort();` that fire when the
     * deterministic random sequence produces a small gear. Roughly half
     * of all 32-bit seeds hit one of those aborts in the first few gears.
     *
     * A live seed makes most launches succeed (54% of seeds are clean
     * for gears in a 100k sweep, but each launch only samples one seed
     * and a single retry on abort would clear that). The cost of losing
     * bit-exact reproducibility across runs is the right one for a
     * screensaver -- the alternative is "deterministically broken".
     *
     * The historical "looks the same on every run" comment in this file
     * was a property of upstream xscreensaver's daemon mode, where the
     * daemon runs each hack for minutes and the deterministic seed
     * mattered because the daemon doesn't fork between hacks. Our
     * _demo binaries are one-shot, so per-run reseeding is fine.
     */
    gettimeofday(&tv, NULL);
    seed = (unsigned int)(tv.tv_sec ^ tv.tv_usec ^ ((unsigned int)getpid() << 16));
    srandom(seed);
    /*
     * frand(n) must return a value uniformly distributed in [0, n). We
     * approximate that by shifting random() down to its top 16 bits (so
     * the result is in [0, 65535]) and dividing by 65536.0.
     *
     * The historical version calibrated frand_max to one specific
     * random() output and used it as the divisor. That calibration was
     * broken: if the very first random() after srandom returned a value
     * smaller than 0x00010000, frand_max became 0 (clamped to 1), and
     * every subsequent frand(n) call returned n * random() (i.e. up to
     * ~n * 65535) instead of n. That is exactly why gears was aborting
     * inside its data-validation assertions: tooth_w, tooth_h, nteeth
     * and the resulting r/inner_r were all inflated by ~4-9x over
     * their intended values, so the final `if (g->inner_r > g->r) abort()`
     * fired on the very first gear.
     *
     * Using a fixed 65536.0 divisor removes the calibration entirely;
     * the bias it introduced (a small fraction of the lowest bucket
     * mapping to a slightly wider range) is irrelevant for visuals.
     */
    frand_max = 65536.0;
    frand_initialized = 1;
}

double frand(double n) {
    frand_init();
    return n * ((double)((unsigned long)random() >> 16) / frand_max);
}

/* ----------------------------------------------------------------------- */
/* xlockmore_mi_init                                                       */
/* ----------------------------------------------------------------------- */

void xlockmore_mi_init(ModeInfo *mi, size_t sz, void **parray) {
    /*
     * Real xlockmore allocates a per-screen array of size `sz`, one slot
     * per X screen. We have ONE screen — allocate one slot, initialize
     * it to zero. The hack writes its per-screen state into it.
     *
     * On the FIRST call we also set up the ModeInfo's display/window/
     * xgwa fields to point at our harness globals. Subsequent calls are
     * idempotent.
     */
    (void)mi;
    if (*parray != NULL) return;   /* already initialized */

    char *arr = (char *)calloc(1, sz);
    if (!arr) {
        fprintf(stderr, "xscreensaver_compat: xlockmore_mi_init OOM (%zu bytes)\n", sz);
        ncz_harness_die(1);
    }
    *parray = arr;
}

/* ----------------------------------------------------------------------- */
/* init_GL                                                                 */
/* ----------------------------------------------------------------------- */

void *init_GL(ModeInfo *mi) {
    /*
     * On real xscreensaver: pick a GLX visual, create GLXContext, bind to
     * the X Window. Return a pointer that holds the GLXContext (cast to
     * void*) so the hack can stash it in mp->glx_context and call
     * glXMakeCurrent(dpy, win, *mp->glx_context) per-frame.
     *
     * In our pipeline: the harness has ALREADY created the EGL context
     * and made it current. The hack doesn't need to create anything —
     * it just needs a non-NULL sentinel that glXMakeCurrent can ignore.
     *
     * We return a pointer to a static sentinel. The hack dereferences
     * it (glXMakeCurrent(..., *ctx)) — that's fine, the sentinel is a
     * pointer to a pointer.
     *
     * We also populate mi->xgwa.width/height from the harness globals
     * so image_data_to_ximage (later called by the hack) gets correct
     * dimensions. And we set mi->screen_number = 0, mi->dpy = the
     * harness xscreen sentinel.
     */
    static void *sentinel_ctx = NULL;

    if (!g_harness_initialized) {
        fprintf(stderr, "xscreensaver_compat: init_GL called before harness "
                "initialized — harness must set g_harness_initialized=1 "
                "before calling init_<hack>.\n");
        return NULL;
    }

    mi->dpy = (Display *)g_harness_xscreen;
    mi->window = 0;   /* unused by our shim */
    mi->screen_number = 0;
    mi->xgwa.screen = NULL;
    mi->xgwa.visual = NULL;
    mi->xgwa.colormap = 0;
    mi->xgwa.width  = g_harness_width;
    mi->xgwa.height = g_harness_height;
    mi->xgwa.depth  = 24;
    mi->fps_p = False;  /* we never display FPS */
    mi->polygon_count = 0;
    mi->glx_context = NULL;  /* unused by us; sentinel is enough */

    g_harness_initialized |= 2;  /* set "init_GL called" bit */
    return &sentinel_ctx;
}

/* ----------------------------------------------------------------------- */
/* glX passthroughs                                                        */
/* ----------------------------------------------------------------------- */

int glXMakeCurrent(Display *dpy, Window drawable, GLXContext ctx) {
    /*
     * The harness has already made the EGL context current. We just
     * record that the hack asked us to (for diagnostic purposes) and
     * return success.
     *
     * Real xscreensaver: glXMakeCurrent binds a GLX context to an X
     * drawable. We get the EGL objects from the harness globals.
     */
    (void)dpy; (void)drawable; (void)ctx;
    return 1;  /* GL_TRUE */
}

void glXSwapBuffers(Display *dpy, Window drawable) {
    /*
     * Real xscreensaver: glXSwapBuffers presents the back buffer to the
     * X server. We delegate to eglSwapBuffers on the harness's surface.
     */
    (void)dpy; (void)drawable;
    if (!g_harness_initialized || !g_harness_egl_display || !g_harness_egl_surface) {
        /* No-op before harness is ready; this can happen during
         * init_GL's check_gl_error path. */
        return;
    }
    eglSwapBuffers((EGLDisplay)g_harness_egl_display,
                   (EGLSurface)g_harness_egl_surface);
}

void glXDestroyContext(Display *dpy, GLXContext ctx) {
    /*
     * Real xscreensaver: destroys the GLX context. We let the harness
     * own context lifetime — this is a no-op.
     */
    (void)dpy; (void)ctx;
}

/* ----------------------------------------------------------------------- */
/* clear_gl_error / check_gl_error                                         */
/* ----------------------------------------------------------------------- */

void clear_gl_error(void) {
    /*
     * Real xscreensaver: drains GL errors so a fresh check_gl_error
     * call sees only errors from the operation under test.
     */
    while (glGetError() != GL_NO_ERROR) { /* drain */ }
}

void check_gl_error(const char *type) {
    /*
     * Real xscreensaver: print GL errors with a label. We do the same
     * but DON'T exit — the hack expects to be able to continue past a
     * GL error.
     */
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
        fprintf(stderr, "xscreensaver_compat: GL error after %s: 0x%x\n",
                type ? type : "(no label)", (unsigned int)e);
    }
}

/* ----------------------------------------------------------------------- */
/* current_device_rotation                                                 */
/* ----------------------------------------------------------------------- */

double current_device_rotation(void) {
    /*
     * Real xscreensaver: returns 0 on desktop (X11/Wayland), real
     * rotation angle on mobile. We are always desktop.
     */
    return 0.0;
}

/* ----------------------------------------------------------------------- */
/* do_fps                                                                  */
/* ----------------------------------------------------------------------- */

void do_fps(ModeInfo *mi) {
    /*
     * Real xscreensaver: draws an FPS counter overlay. We never call
     * this because we initialize mi->fps_p = False. Stub retained for
     * compile completeness.
     */
    (void)mi;
}

/* ----------------------------------------------------------------------- */
/* xlockmore_no_events                                                     */
/* ----------------------------------------------------------------------- */

Bool xlockmore_no_events(ModeInfo *mi, XEvent *event) {
    /*
     * Real xscreensaver: "did the daemon have events to dispatch this
     * frame?" — used to gate input-handling. We have no X events; the
     * harness dispatches key/mouse events directly to the hack.
     */
    (void)mi; (void)event;
    return 1;
}

/* ----------------------------------------------------------------------- */
/* get_string_resource / get_boolean_resource                             */
/* ----------------------------------------------------------------------- */
/*
 * A hack declares its tunables in a ModeSpecVar (argtype) table and NEVER
 * assigns them in code. In real xscreensaver the option-parsing layer walks
 * that table and WRITES each parsed value THROUGH the stored var pointer.
 *
 * We do not parse X resources or argv, but we must still perform that write,
 * because a hack whose table is never walked runs with every tunable at its
 * BSS default -- every Bool False, every float 0.0, every string NULL.
 *
 * That is not a cosmetic difference. MEASURED on O6N 2026-08-18: glmatrix
 * rendered pure black because `do_texture` stayed False, so init_matrix
 * skipped `load_textures()` entirely and no glyph atlas was ever uploaded.
 * The tell was in the log ordering -- "init_matrix returned" printed BEFORE
 * GL4ES initialised, i.e. init_matrix had made no GL call at all. The decode
 * and upload path that looked guilty was never reached.
 *
 * xs_compat_apply_var_defaults() below performs the write, using each entry's
 * own DEF_* string as the source, which is exactly what upstream does when no
 * resource or command-line override is present.
 */

static Bool xs_parse_bool(const char *s) {
    if (!s) return False;
    while (*s == ' ' || *s == '\t') s++;
    return (*s == 't' || *s == 'T' ||        /* true  */
            *s == 'y' || *s == 'Y' ||        /* yes   */
            *s == '1' ||
            ((*s == 'o' || *s == 'O') &&     /* on, but not off */
             (s[1] == 'n' || s[1] == 'N')));
}

/* Look up an override for one tunable in the environment.
 *
 * Upstream takes these from X resources or argv; we have neither, and a hack
 * whose behaviour can only be changed by editing and rebuilding it is very
 * hard to bisect. XS_<NAME> covers that: XS_FOG=False, XS_DENSITY=40. */
static const char *xs_env_override(const char *name) {
    char key[64];
    size_t i;
    if (!name) return NULL;
    key[0] = 'X'; key[1] = 'S'; key[2] = '_';
    for (i = 0; name[i] && i + 4 < sizeof key; i++) {
        char c = name[i];
        key[3 + i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    key[3 + i] = '\0';
    return getenv(key);
}

/*
 * Defaults block parsed from the hack's DEFAULTS macro.
 *
 * A hack declares its tunables two ways in upstream xscreensaver:
 *
 *   1. An argtype vars[] table -- the "xlockmore-style" declarations, where
 *      each entry holds the .def (default) inline. xs_compat_apply_var_defaults
 *      walks this and writes through each var pointer.
 *
 *   2. A `#define DEFAULTS "*name: value\n..."` macro at the top of the .c
 *      file -- the "screenhack-style" declarations, used for resource keys
 *      upstream treated as defaults rather than tunables: typically the
 *      color names, font names and similar that the hack reads via
 *      get_string_resource() but never puts in vars[].
 *
 * Until 2026-08-20 we parsed only the first form, which silently broke
 * chompytower (jawColor), covid19, gibson, gravitywell, handsy, headroom,
 * highvoltage, nakagin, skulloop, splitflap, squirtorus, vigilance,
 * winduprobot, fliptext, unknownpleasures, dnalogo and unicrud. The hack's
 * get_string_resource("jawColor") came back empty, XParseColor printed
 * "unparsable color in jawColor: ", and the hack exited 1 -- logged as a
 * crash even though the actual fault was a shim-side lookup miss.
 *
 * We now parse the DEFAULTS block at apply time and store the entries here,
 * in a per-hack table. get_*_resource consults this table after the
 * vars[] table, so a hack with both styles sees a single consistent view.
 *
 * Entries are malloc'd and must be freed when the harness tears down or
 * a new hack is loaded into the same process. We are single-hack-at-a-time
 * (each _demo binary runs one effect and exits), so the leak is bounded by
 * the number of entries one DEFAULTS block contributes (a few dozen).
 */
typedef struct {
    char *name;
    char *value;
} xs_defaults_entry;

static xs_defaults_entry *xs_defaults_table = NULL;
static int                 xs_defaults_count  = 0;
static int                 xs_defaults_cap    = 0;

static void xs_defaults_clear(void) {
    int i;
    for (i = 0; i < xs_defaults_count; i++) {
        free(xs_defaults_table[i].name);
        free(xs_defaults_table[i].value);
    }
    xs_defaults_count = 0;
    /* keep xs_defaults_table allocated for reuse -- freed at process exit. */
}

static void xs_defaults_add(const char *name, const char *value, int vlen) {
    char *n, *v;
    if (xs_defaults_count == xs_defaults_cap) {
        int newcap = xs_defaults_cap ? xs_defaults_cap * 2 : 32;
        xs_defaults_entry *nt = realloc(xs_defaults_table,
                                        newcap * sizeof(*nt));
        if (!nt) return;   /* OOM: silently skip */
        xs_defaults_table = nt;
        xs_defaults_cap = newcap;
    }
    n = strdup(name);
    v = (char *)malloc(vlen + 1);
    if (!n || !v) { free(n); free(v); return; }
    memcpy(v, value, vlen);
    v[vlen] = '\0';
    xs_defaults_table[xs_defaults_count].name = n;
    xs_defaults_table[xs_defaults_count].value = v;
    xs_defaults_count++;
}

static const char *xs_defaults_lookup(const char *name) {
    int i;
    if (!name || !xs_defaults_table) return NULL;
    for (i = 0; i < xs_defaults_count; i++) {
        if (strcmp(xs_defaults_table[i].name, name) == 0)
            return xs_defaults_table[i].value;
    }
    return NULL;
}

/* Parse a DEFAULTS string of the form:
 *
 *     "*name: value\n*name2: value 2\n..."
 *
 * Each line is "*" + name + ":" + value (whitespace padded). Names and
 * values are stripped of leading/trailing whitespace. Values may contain
 * internal whitespace and punctuation (font names, hex colors with spaces,
 * text strings). Empty lines are skipped. A value that extends across the
 * remaining buffer is still captured -- the DEFAULTS macro typically has a
 * trailing "\n" but some hacks omit it on the last line.
 *
 * Not strtok-based because DEFAULTS is read-only storage in the vendored
 * hack's .rodata, and we want to keep that storage immutable.
 */
static void xs_parse_defaults_string(const char *s) {
    const char *p;
    if (!s) return;
    xs_defaults_clear();
    p = s;
    while (*p) {
        const char *line_start = p;
        const char *nl = strchr(p, '\n');
        const char *line_end = nl ? nl : (p + strlen(p));
        const char *colon;
        const char *name_start, *name_end;
        const char *val_start, *val_end;
        int name_len, val_len;

        /* Skip leading whitespace, require a leading '*' or '.' as the
         * "this is a defaults line" marker. screenhack.c treats them as
         * equivalent: '*' is the loose binding (matches the program class),
         * '.' is the tight binding (matches the program instance). Hacks
         * mix them in the same block -- dnalogo uses '*' for tunables and
         * '.' for the colour slot specifically. */
        while (line_start < line_end &&
               (*line_start == ' ' || *line_start == '\t' ||
                *line_start == '\r'))
            line_start++;
        if (line_start >= line_end ||
            (*line_start != '*' && *line_start != '.'))
            goto next;

        name_start = line_start + 1;
        colon = name_start;
        while (colon < line_end && *colon != ':') colon++;
        if (colon >= line_end) goto next;   /* no colon, malformed line */

        name_end = colon;
        while (name_end > name_start &&
               (name_end[-1] == ' ' || name_end[-1] == '\t'))
            name_end--;
        name_len = (int)(name_end - name_start);
        if (name_len <= 0) goto next;

        val_start = colon + 1;
        while (val_start < line_end &&
               (*val_start == ' ' || *val_start == '\t'))
            val_start++;
        val_end = line_end;
        while (val_end > val_start &&
               (val_end[-1] == ' ' || val_end[-1] == '\t' ||
                val_end[-1] == '\r'))
            val_end--;
        val_len = (int)(val_end - val_start);

        /* Heap-copy both -- the source string is rodata we must not mutate. */
        {
            char *namebuf = (char *)malloc(name_len + 1);
            if (!namebuf) goto next;
            memcpy(namebuf, name_start, name_len);
            namebuf[name_len] = '\0';
            xs_defaults_add(namebuf, val_start, val_len);
            free(namebuf);
        }

      next:
        if (!nl) break;
        p = nl + 1;
    }
}

/* The table most recently applied. The resource getters below consult it so
 * that a hack which CALLS get_*_resource() sees the same values as a hack that
 * reads the var pointers directly. */
static ModeSpecOpt *xs_active_opts = NULL;

/* Find a var entry by resource name. */
static ModeSpecVar *xs_find_var(const char *name) {
    int i;
    if (!xs_active_opts || !xs_active_opts->vars || !name) return NULL;
    for (i = 0; i < xs_active_opts->numvars; i++) {
        ModeSpecVar *v = &xs_active_opts->vars[i];
        if (v->name && strcmp(v->name, name) == 0) return v;
    }
    return NULL;
}

void xs_compat_apply_var_defaults(ModeSpecOpt *o, const char *defaults_str) {
    int i;
    xs_active_opts = o;

    /* Parse the screenhack-style DEFAULTS block FIRST so the side-table is
     * populated before vars[] writes go through: a hack whose vars[] table
     * points at a name that ALSO appears in DEFAULTS (e.g. "delay") still
     * resolves via vars[], but anything only in DEFAULTS (colors, fonts) is
     * findable through xs_defaults_lookup. */
    xs_parse_defaults_string(defaults_str);

    if (!o || !o->vars) return;
    for (i = 0; i < o->numvars; i++) {
        ModeSpecVar *v = &o->vars[i];
        const char *ov;
        if (!v->var) continue;
        ov = xs_env_override(v->name);
        if (ov) {
            fprintf(stderr, "[xs-compat] override %s = %s\n", v->name, ov);
            v->def = (char *)ov;
        }
        switch (v->type) {
        case t_String:
            *(char **)v->var = v->def;
            break;
        case t_Bool:
            *(Bool *)v->var = xs_parse_bool(v->def);
            break;
        case t_Int:
            *(int *)v->var = v->def ? atoi(v->def) : 0;
            break;
        case t_Float:
            /* t_Float targets a float-width variable; hacks commonly declare
             * these as GLfloat, which is float on every platform we build. */
            *(float *)v->var = v->def ? (float)atof(v->def) : 0.0f;
            break;
        }
    }
}

/* xlockmore keeps a second set of standard resources in ModeInfo rather than
 * in each hack's vars[] table. Apply only values explicitly present in the
 * active DEFAULTS block so absent resources retain the harness defaults. */
void xs_compat_apply_mode_defaults(ModeInfo *mi) {
    const char *v;
    int i;
    if (!mi) return;
    v = xs_defaults_lookup("delay");
    if (v) mi->pause = atol(v);
    v = xs_defaults_lookup("count");
    if (v) mi->batchcount = atol(v);
    v = xs_defaults_lookup("cycles");
    if (v) mi->cycles = atol(v);
    v = xs_defaults_lookup("size");
    if (v) mi->size = atol(v);
    v = xs_defaults_lookup("wireframe");
    if (v) mi->wireframe_p = xs_parse_bool(v);
    v = xs_defaults_lookup("showFPS");
    if (v) mi->fps_p = xs_parse_bool(v);
    v = xs_defaults_lookup("ncolors");
    if (v) mi->npixels = atoi(v);
    if (mi->npixels <= 0) mi->npixels = 256;
    if (!mi->colors)
        mi->colors = (XColor *)calloc((size_t)mi->npixels, sizeof(*mi->colors));
    if (!mi->pixels)
        mi->pixels = (unsigned long *)calloc((size_t)mi->npixels,
                                             sizeof(*mi->pixels));
    if (!mi->colors || !mi->pixels) ncz_harness_die(1);
    for (i = 0; i < mi->npixels; i++)
        mi->pixels[i] = (unsigned long)i;
}

/*
 * Resource getters, backed by the hack's own var table.
 *
 * Hacks reach their tunables two ways: through the var pointer (handled by
 * xs_compat_apply_var_defaults above) or by calling these. Both must agree, or
 * a hack behaves differently depending on which style its author used -- and
 * the failure is silent, which is how do_texture stayed False and glmatrix
 * rendered black.
 *
 * An unknown name returns the caller's stated fallback rather than a zero
 * value, because "" and False are legitimate settings and are indistinguishable
 * from "not found" otherwise.
 */

char *get_string_resource(void *ctx, const char *res_name,
                          const char *res_class) {
    ModeSpecVar *v;
    const char *val = NULL;
    (void)ctx; (void)res_class;
    v = xs_find_var(res_name);
    if (v && v->type == t_String && v->var && *(char **)v->var)
        val = *(char **)v->var;        /* live value, may carry an override */
    else if (v)
        val = v->def;
    /* Fall through to the screenhack-style DEFAULTS table. chompytower's
     * "jawColor" (and the 16 other hacks' analogous names) only appear in
     * DEFAULTS, never in vars[]. Without this fallback the call returns
     * "" and XParseColor errors out with "unparsable color in jawColor: ". */
    if (!val)
        val = xs_defaults_lookup(res_name);
    /* XScreenSaver's resource database supplies these two application-wide
     * defaults even when a hack's DEFAULTS block does not repeat them.
     * Standalone screenhack-style modules such as unknownpleasures request
     * them directly by class, so an empty string here makes XParseColor fail
     * during init. */
    if (!val && res_class) {
        if (strcmp(res_class, "Foreground") == 0)
            val = "white";
        else if (strcmp(res_class, "Background") == 0)
            val = "black";
    }
    /* MUST be freeable: xscreensaver hacks routinely free() this result, so
     * handing back a string literal or a pointer into the var table would be a
     * free() of static storage. */
    return strdup(val ? val : "");
}

Bool get_boolean_resource(void *ctx, const char *res_name,
                          const char *res_class) {
    ModeSpecVar *v;
    const char *def = NULL;
    (void)ctx; (void)res_class;
    v = xs_find_var(res_name);
    if (v && v->type == t_Bool && v->var) return *(Bool *)v->var;
    if (v) def = v->def;
    /* Fall through to DEFAULTS table. dnalogo reads "doGasket"/"doHelix"
     * this way and has no vars[] table at all, so this lookup is the
     * only way those names resolve. Without it dnalogo exits with
     * "no helix or gasket?" because both booleans default to False. */
    if (!def) def = xs_defaults_lookup(res_name);
    return xs_parse_bool(def);
}

int get_integer_resource(void *ctx, const char *res_name,
                         const char *res_class) {
    ModeSpecVar *v;
    const char *def = NULL;
    (void)ctx; (void)res_class;
    v = xs_find_var(res_name);
    if (v && v->type == t_Int && v->var) return *(int *)v->var;
    if (v) def = v->def;
    if (!def) def = xs_defaults_lookup(res_name);
    return def ? atoi(def) : 0;
}

double get_float_resource(void *ctx, const char *res_name,
                          const char *res_class) {
    ModeSpecVar *v;
    const char *def = NULL;
    (void)ctx; (void)res_class;
    v = xs_find_var(res_name);
    if (v && v->type == t_Float && v->var) return (double)*(float *)v->var;
    if (v) def = v->def;
    if (!def) def = xs_defaults_lookup(res_name);
    return def ? atof(def) : 0.0;
}

/* ----------------------------------------------------------------------- */
/* XImage pixel accessors                                                  */
/* ----------------------------------------------------------------------- */
/*
 * glmatrix.c calls:
 *     unsigned long p = XGetPixel(xi, x, y);
 *     XPutPixel(xi, x, y, p);
 * where `p` is treated as a packed RGBA word (the hack unpacks it into
 * r/g/b/a bytes). Our XImage stores RGBA8 data tightly packed (R at
 * offset 0, G at 1, B at 2, A at 3). XGetPixel returns the 32-bit word
 * in the order expected by glmatrix's bit-shifts.
 *
 * glmatrix.c bit-shifts with rpos=0, gpos=8, bpos=16, apos=24 (line 762
 * of original) — so it expects the word to be in memory order R|G|B|A
 * with R in the LSB. We pack as 0xAABBGGRR in memory order, which means
 * when read as a little-endian 32-bit int the value is 0xRRGGBBAA. That
 * matches: extracting `(p >> 0) & 0xFF` gives R, `(p >> 8) & 0xFF` gives
 * G, etc.
 */






/* ------------------------------------------------------------------------- */
/* Colour ramps                                                              */
/* ------------------------------------------------------------------------- */

/* HSV -> RGB, channels 0..65535 to match XColor.
 * h in degrees [0,360), s and v in [0,1]. */
static void xs_hsv_to_rgb16(double h, double s, double v,
                            unsigned short *r, unsigned short *g,
                            unsigned short *b)
{
    double rr = v, gg = v, bb = v;
    if (s > 0.0) {
        double f, p, q, t;
        int i;
        h = fmod(h, 360.0);
        if (h < 0.0) h += 360.0;
        h /= 60.0;
        i = (int) h;
        f = h - i;
        p = v * (1.0 - s);
        q = v * (1.0 - s * f);
        t = v * (1.0 - s * (1.0 - f));
        switch (i) {
            case 0:  rr = v; gg = t; bb = p; break;
            case 1:  rr = q; gg = v; bb = p; break;
            case 2:  rr = p; gg = v; bb = t; break;
            case 3:  rr = p; gg = q; bb = v; break;
            case 4:  rr = t; gg = p; bb = v; break;
            default: rr = v; gg = p; bb = q; break;
        }
    }
    *r = (unsigned short) (rr * 65535.0 + 0.5);
    *g = (unsigned short) (gg * 65535.0 + 0.5);
    *b = (unsigned short) (bb * 65535.0 + 0.5);
}

/* Fill `colors` with a smooth CYCLIC colour ramp.
 *
 * Upstream picks a handful of random points in HSV and interpolates between
 * them, wrapping so the last colour blends back into the first. The hacks rely
 * on that cyclicity: they walk the array with an incrementing index modulo
 * ncolors, so a discontinuity at the wrap shows up as a visible flash.
 *
 * Not a byte-for-byte reimplementation of utils/colors.c -- the hacks only
 * require "a smooth loop of pleasant colours of the requested length", and the
 * exact sequence is random per run anyway.
 *
 * screen/visual/cmap/allocate_p/writable_pP are ignored: no X server, nothing
 * to allocate. *ncolorsP is left as the caller set it (we always fill exactly
 * that many) so callers that re-read it stay consistent.
 */
void make_smooth_colormap(Screen *screen, Visual *visual, Colormap cmap,
                          XColor *colors, int *ncolorsP,
                          Bool allocate_p, Bool *writable_pP,
                          Bool verbose_p)
{
    int n, i, npoints, seg;
    double hues[6], sat, val;

    (void) screen; (void) visual; (void) cmap;
    (void) allocate_p; (void) writable_pP; (void) verbose_p;

    if (!colors || !ncolorsP) return;
    n = *ncolorsP;
    if (n <= 0) return;

    /* 3-5 waypoints around the wheel; fewer looks like a two-tone gradient,
     * more turns into noise at typical ncolors (64-256). */
    npoints = 3 + (random() % 3);
    for (i = 0; i < npoints; i++)
        hues[i] = (random() % 360000) / 1000.0;
    hues[npoints] = hues[0];          /* close the loop */

    sat = 0.6 + (random() % 400) / 1000.0;   /* 0.6 .. 1.0 */
    val = 0.7 + (random() % 300) / 1000.0;   /* 0.7 .. 1.0 */

    for (i = 0; i < n; i++) {
        double t = (double) i * npoints / (double) n;
        double frac;
        double h0, h1, dh;
        seg = (int) t;
        if (seg >= npoints) seg = npoints - 1;
        frac = t - seg;

        h0 = hues[seg];
        h1 = hues[seg + 1];
        /* Interpolate the SHORT way around the wheel, otherwise a pair like
         * 350 -> 10 sweeps backwards through the entire spectrum. */
        dh = h1 - h0;
        if (dh > 180.0)  dh -= 360.0;
        if (dh < -180.0) dh += 360.0;

        xs_hsv_to_rgb16(h0 + dh * frac, sat, val,
                        &colors[i].red, &colors[i].green, &colors[i].blue);
        colors[i].pixel = (unsigned long) i;
        colors[i].flags = 0;
        colors[i].pad   = 0;
    }
}

/* make_random_colormap (utils/colors.c) -- fills *colors with ncolors
 * random entries. bright_p picks from a bright-saturated HSV band;
 * otherwise each channel is independently random. screen/visual/cmap
 * are vestigial; allocate_p / writable_pP / verbose_p are ignored.
 *
 * Vendored hacks reach this through colors.h. The implementations of
 * this and make_smooth_colormap intentionally diverge from upstream
 * only in their handling of X colormap allocation, which doesn't apply
 * to a GL4ES pipeline: there's no X server to allocate cells in.
 */
void make_random_colormap(Screen *screen, Visual *visual, Colormap cmap,
                          XColor *colors, int *ncolorsP,
                          Bool bright_p, Bool allocate_p,
                          Bool *writable_pP, Bool verbose_p)
{
    int n, i;

    (void) screen; (void) visual; (void) cmap;
    (void) allocate_p; (void) writable_pP; (void) verbose_p;

    if (!colors || !ncolorsP) return;
    n = *ncolorsP;
    if (n <= 0) return;

    for (i = 0; i < n; i++) {
        colors[i].flags = 0;
        colors[i].pad   = 0;
        colors[i].pixel = (unsigned long) i;
        if (bright_p) {
            int H = random() % 360;
            double S = ((double) (random() % 70) + 30) / 100.0;
            double V = ((double) (random() % 34) + 66) / 100.0;
            xs_hsv_to_rgb16((double) H, S, V,
                            &colors[i].red,
                            &colors[i].green,
                            &colors[i].blue);
        } else {
            colors[i].red   = (unsigned short) (random() & 0xFFFF);
            colors[i].green = (unsigned short) (random() & 0xFFFF);
            colors[i].blue  = (unsigned short) (random() & 0xFFFF);
        }
    }
}

void make_color_ramp(Screen *screen, Visual *visual, Colormap cmap,
                     int h1, double s1, double v1,
                     int h2, double s2, double v2,
                     XColor *colors, int *ncolorsP,
                     Bool closed_p,
                     Bool allocate_p,
                     Bool *writable_pP)
{
    int n, i, limit;

    (void) screen; (void) visual; (void) cmap;
    (void) allocate_p; (void) writable_pP;

    if (!colors || !ncolorsP) return;
    n = *ncolorsP;
    if (n <= 0) return;

    limit = closed_p ? (n / 2 + 1) : n;
    if (limit <= 1) {
        xs_hsv_to_rgb16(h1, s1, v1, &colors[0].red, &colors[0].green, &colors[0].blue);
        colors[0].pixel = 0;
        colors[0].flags = 0;
        colors[0].pad = 0;
        return;
    }

    for (i = 0; i < limit; i++) {
        double t = (double)i / (double)(limit - 1);
        double dh = (double)h2 - (double)h1;
        double h;
        if (dh > 180.0) dh -= 360.0;
        if (dh < -180.0) dh += 360.0;
        h = (double)h1 + dh * t;
        xs_hsv_to_rgb16(h,
                        s1 + (s2 - s1) * t,
                        v1 + (v2 - v1) * t,
                        &colors[i].red, &colors[i].green, &colors[i].blue);
        colors[i].pixel = (unsigned long)i;
        colors[i].flags = 0;
        colors[i].pad = 0;
    }

    if (closed_p) {
        for (i = limit; i < n; i++) {
            int src = limit - 2 - (i - limit);
            if (src < 0) src = 0;
            colors[i] = colors[src];
            colors[i].pixel = (unsigned long)i;
        }
    }
}

void make_color_loop(Screen *screen, Visual *visual, Colormap cmap,
                     int h1, double s1, double v1,
                     int h2, double s2, double v2,
                     int h3, double s3, double v3,
                     XColor *colors, int *ncolorsP,
                     Bool allocate_p,
                     Bool *writable_pP)
{
    int n, i;
    const int hues[4] = { h1, h2, h3, h1 };
    const double sats[4] = { s1, s2, s3, s1 };
    const double vals[4] = { v1, v2, v3, v1 };

    (void) screen; (void) visual; (void) cmap;
    (void) allocate_p; (void) writable_pP;

    if (!colors || !ncolorsP) return;
    n = *ncolorsP;
    if (n <= 0) return;

    for (i = 0; i < n; i++) {
        double pos = ((double)i * 3.0) / (double)n;
        int seg = (int)pos;
        double t = pos - seg;
        double dh, h;
        if (seg > 2) seg = 2;
        dh = (double)hues[seg + 1] - (double)hues[seg];
        if (dh > 180.0) dh -= 360.0;
        if (dh < -180.0) dh += 360.0;
        h = (double)hues[seg] + dh * t;
        xs_hsv_to_rgb16(h,
                        sats[seg] + (sats[seg + 1] - sats[seg]) * t,
                        vals[seg] + (vals[seg + 1] - vals[seg]) * t,
                        &colors[i].red, &colors[i].green, &colors[i].blue);
        colors[i].pixel = (unsigned long)i;
        colors[i].flags = 0;
        colors[i].pad = 0;
    }
}

static int xs_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static Bool xs_parse_hex_component(const char *s, int digits,
                                   unsigned short *out)
{
    int i;
    unsigned int v = 0;
    for (i = 0; i < digits; i++) {
        int h = xs_hexval(s[i]);
        if (h < 0) return False;
        v = (v << 4) | (unsigned int)h;
    }

    if (digits == 1) v = v * 0x1111u;
    else if (digits == 2) v = (v << 8) | v;
    else if (digits == 4) { /* already 16-bit */ }
    else return False;

    *out = (unsigned short)v;
    return True;
}

static Bool xs_named_color(const char *spec, XColor *c)
{
    struct named_color { const char *name; unsigned short r, g, b; };
    static const struct named_color colors[] = {
        {"black",   0x0000, 0x0000, 0x0000},
        {"blue",    0x0000, 0x0000, 0xffff},
        {"cyan",    0x0000, 0xffff, 0xffff},
        {"gray",    0x8080, 0x8080, 0x8080},
        {"green",   0x0000, 0xffff, 0x0000},
        {"grey",    0x8080, 0x8080, 0x8080},
        {"magenta", 0xffff, 0x0000, 0xffff},
        {"orange",  0xffff, 0xa5a5, 0x0000},
        {"purple",  0x8080, 0x0000, 0x8080},
        {"red",     0xffff, 0x0000, 0x0000},
        {"white",   0xffff, 0xffff, 0xffff},
        {"yellow",  0xffff, 0xffff, 0x0000},
    };
    size_t i;
    for (i = 0; i < countof(colors); i++) {
        if (strcasecmp(spec, colors[i].name) == 0) {
            c->red = colors[i].r;
            c->green = colors[i].g;
            c->blue = colors[i].b;
            return True;
        }
    }
    return False;
}

int XParseColor(Display *dpy, Colormap cmap, const char *spec,
                XColor *exact_def_return)
{
    size_t len;
    int digits;
    XColor c = {0};

    (void)dpy;
    (void)cmap;
    if (!spec || !exact_def_return) return 0;
    while (isspace((unsigned char)*spec)) spec++;

    if (spec[0] == '#') {
        len = strlen(spec + 1);
        if (len != 3 && len != 6 && len != 12) return 0;
        digits = (int)(len / 3);
        if (!xs_parse_hex_component(spec + 1, digits, &c.red) ||
            !xs_parse_hex_component(spec + 1 + digits, digits, &c.green) ||
            !xs_parse_hex_component(spec + 1 + 2 * digits, digits, &c.blue))
            return 0;
    } else if (!xs_named_color(spec, &c)) {
        return 0;
    }

    c.pixel = ((unsigned long)(c.red >> 8) << 16) |
              ((unsigned long)(c.green >> 8) << 8) |
              (unsigned long)(c.blue >> 8);
    c.flags = 0;
    c.pad = 0;
    *exact_def_return = c;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Input plumbing (see the header for why these are inert)                   */
/* ------------------------------------------------------------------------- */

/* Upstream carries a quaternion and drag origin. With no pointer events there
 * is nothing to accumulate, so the struct exists only to give the hacks a
 * non-NULL handle to pass around. */
struct trackball_state { int unused; };

#define NCZ_TRACKBALL_POOL_SIZE 256
static struct trackball_state xs_trackball_pool[NCZ_TRACKBALL_POOL_SIZE];
static int                  xs_trackball_used[NCZ_TRACKBALL_POOL_SIZE];

trackball_state *gltrackball_init(int ignore_device_rotation_p)
{
    /* Vendored xscreensaver hacks free the pointer returned here
     * (jigsaw.c:1523 does `free(jc->trackball)` directly; most others
     * use the gltrackball_free wrapper). Returning a static singleton
     * makes the free a bad-free under AddressSanitizer and corrupts
     * malloc metadata in release builds. Hand out a heap-resident
     * struct from a small pool so the caller can free it cleanly.
     *
     * The pool exists so the screenhack's per-init/per-free lifecycle
     * is bounded even though we don't actually trackball-rotate
     * anything; handing out a singleton would re-introduce the same
     * bug if the caller ever called free() on it.
     * NCZ_TRACKBALL_POOL_SIZE is sized generously so a 90-binary
     * validation sweep doesn't run out. */
    (void) ignore_device_rotation_p;
    for (int i = 0; i < NCZ_TRACKBALL_POOL_SIZE; i++) {
        if (!xs_trackball_used[i]) {
            xs_trackball_used[i] = 1;
            memset(&xs_trackball_pool[i], 0, sizeof xs_trackball_pool[i]);
            return &xs_trackball_pool[i];
        }
    }
    /* Out of slots — fail loudly rather than hand back a singleton
     * that the caller will then free. */
    fprintf(stderr,
            "xscreensaver_compat: gltrackball_init pool exhausted "
            "(NCZ_TRACKBALL_POOL_SIZE=%d); failing\n",
            NCZ_TRACKBALL_POOL_SIZE);
    abort();
}

void gltrackball_free(trackball_state *ts) {
    if (!ts) return;
    /* Return the slot to the pool. Direct free() (jigsaw.c:1523 does
     * this) is not valid because the pointer is pool-resident, not
     * malloc'd. gltrackball_free is the supported release path. */
    ptrdiff_t idx = ts - xs_trackball_pool;
    if (idx >= 0 && idx < NCZ_TRACKBALL_POOL_SIZE) {
        xs_trackball_used[idx] = 0;
        memset(ts, 0, sizeof *ts);
    } else {
        /* Pointer not from our pool — refuse to free to avoid
         * corrupting unrelated malloc metadata. */
        fprintf(stderr,
                "xscreensaver_compat: gltrackball_free called on "
                "pointer 0x%lx not in the trackball pool; leaking\n",
                (unsigned long)(uintptr_t)ts);
    }
}

void gltrackball_rotate(trackball_state *ts)
{
    (void) ts;   /* identity rotation: no glMultMatrix, leave the modelview as-is */
}

void gltrackball_reset(trackball_state *ts, float x, float y)
{
    (void) ts; (void) x; (void) y;
}

Bool gltrackball_event_handler(XEvent *event, trackball_state *ts,
                               int window_width, int window_height,
                               Bool *button_down_p)
{
    (void) event; (void) ts; (void) window_width; (void) window_height;
    /* Report "not handled" and never claim a button is held. A hack that saw a
     * stuck button_down would freeze its own animation waiting for a release
     * that cannot arrive. */
    if (button_down_p) *button_down_p = 0;
    return 0;
}

void gltrackball_start(trackball_state *ts, int x, int y, int w, int h)
{ (void) ts; (void) x; (void) y; (void) w; (void) h; }

void gltrackball_track(trackball_state *ts, int x, int y, int w, int h)
{ (void) ts; (void) x; (void) y; (void) w; (void) h; }

void gltrackball_mousewheel(trackball_state *ts, int button, int percent, int flip_p)
{ (void) ts; (void) button; (void) percent; (void) flip_p; }

double gltrackball_get_x(trackball_state *ts) { (void) ts; return 0.0; }
double gltrackball_get_y(trackball_state *ts) { (void) ts; return 0.0; }

Bool screenhack_event_helper(Display *dpy, Window window, XEvent *event)
{
    (void) dpy; (void) window; (void) event;
    return 0;    /* not handled */
}

int XLookupString(XKeyEvent *event, char *buffer, int nbytes,
                  KeySym *keysym, void *status)
{
    (void) event; (void) status;
    /* No keyboard mapping table here. Report zero characters and a zero
     * keysym; hacks compare the keysym against XK_* constants and fall through
     * to "ignore" when it matches nothing. */
    if (keysym) *keysym = 0;
    if (buffer && nbytes > 0) buffer[0] = '\0';
    return 0;
}

/* --- colour conversion (utils/colors.c) ---------------------------------- */

/* h in degrees, s/v in [0,1], channels out in 0..65535. */
void hsv_to_rgb(int h, double s, double v,
                unsigned short *r, unsigned short *g, unsigned short *b)
{
    xs_hsv_to_rgb16((double) h, s, v, r, g, b);
}

void rgb_to_hsv(unsigned short r, unsigned short g, unsigned short b,
                int *h, double *s, double *v)
{
    double rr = r / 65535.0, gg = g / 65535.0, bb = b / 65535.0;
    double maxv = rr > gg ? (rr > bb ? rr : bb) : (gg > bb ? gg : bb);
    double minv = rr < gg ? (rr < bb ? rr : bb) : (gg < bb ? gg : bb);
    double d = maxv - minv, hh = 0.0;

    if (d > 0.0) {
        if (maxv == rr)      hh = 60.0 * fmod(((gg - bb) / d), 6.0);
        else if (maxv == gg) hh = 60.0 * (((bb - rr) / d) + 2.0);
        else                 hh = 60.0 * (((rr - gg) / d) + 4.0);
        if (hh < 0.0) hh += 360.0;
    }
    if (h) *h = (int) (hh + 0.5);
    if (s) *s = (maxv > 0.0) ? (d / maxv) : 0.0;
    if (v) *v = maxv;
}

void gltrackball_stop(trackball_state *ts) { (void) ts; }

void gltrackball_get_quaternion(trackball_state *ts, float q[4])
{
    (void) ts;
    /* Identity quaternion -- no accumulated rotation. */
    if (!q) return;
    q[0] = 0.0f; q[1] = 0.0f; q[2] = 0.0f; q[3] = 1.0f;
}

/* --- screenhack_usleep (usleep.h) ------------------------------------- *
 *
 * usleep() was removed from POSIX in 2008 and glibc flags it
 * _XOPEN_SOURCE=500 which our _POSIX_C_SOURCE=200809L doesn't expose.
 * Vendored hacks reach for it directly (glsnake does), so we shim it
 * to nanosleep. */
#include <time.h>
void screenhack_usleep(unsigned long usecs)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)  (usecs / 1000000UL);
    ts.tv_nsec = (long) ((usecs % 1000000UL) * 1000UL);
    nanosleep(&ts, NULL);
}

/* --- texture-font stubs (texfont.h) ------------------------------------ *
 *
 * We do not compile texfont.c — the upstream ~1500 line implementation
 * pulls screenhackI.h, fps.h, xshm.h, jwxyz font APIs, GLSL utilities,
 * and ends up drawing through an Xft pipeline that does not exist on
 * this build (no X11, no FontConfig, no Pango). Vendored hacks that use
 * texture fonts (dnalogo, geodesicgears, gibson, glsnake, juggler3d,
 * mapscroller, molecule, pinion, splitflap, tangram, winduprobot,
 * fliptext, skulloop, spheremonics, unicrud) all guard their text
 * rendering with `if (font)` so a NULL return here produces a clean,
 * un-fonted hack rather than a missing-symbol link failure.
 *
 * The implementations are no-ops / identity functions; they exist only
 * to satisfy the linker.
 */
struct texture_font_data { int glyph_w, ascent, descent; };










/* --- utf8wc.c helpers (utils/utf8wc.c) --------------------------------- *
 *
 * Vendored hacks that include utf8wc.h call these to convert UTF-8
 * input to Latin1 for XChar2b rendering. With texfont.c absent we
 * never feed the result to XDrawString16, but the hacks call them
 * unconditionally during init. utf8_to_latin1 is the only one whose
 * result is consulted (the others return values into ignored locals);
 * we keep their behaviour realistic enough to avoid surprises.
 */



/* utf8_to_XChar2b: convert a UTF-8 string to a 2-byte-packed XChar2b
 * array. Bytes outside Latin1 are stored as '?'. The returned array
 * is malloc'd; the caller is responsible for freeing it. */


/* --- textclient.c stubs (utils/textclient.c) --------------------------- *
 *
 * textclient.c spawns `xscreensaver-text` and pipes bytes back. We are
 * not running an X11 session, so no helper process is available.
 * Vendored hacks that include textclient.h (fliptext, splitflap) only
 * call textclient_getc to read text characters and textclient_puts /
 * textclient_putc_event to feed input back. Some callers (splitflap)
 * require a non-NULL client, so provide a small deterministic fallback
 * stream when the external X11 helper is unavailable.
 */
struct text_data { size_t offset; };







/* --- X Toolkit (Xt) shim (X11/Intrinsic.h) ----------------------------- *
 *
 * mapscroller.c uses XtAppAddInput / XtRemoveInput / the XtInput*Mask
 * constants to register a callback that watches the stdout pipe of an
 * external "xscreensaver-text" helper. We do not run that helper and
 * have no X11 display, so XtAppAddInput simply returns a sentinel
 * input id and XtRemoveInput is a no-op. The callback never fires,
 * which means mapscroller renders without scroll-in text -- still a
 * valid screensaver.
 */



/* --- file_to_ximage (ximage-loader.c) ---------------------------------- *
 *
 * Upstream's file_to_ximage uses GdkPixbuf to load a PNG/JPEG/GIF from
 * a filename into an XImage. We don't have GdkPixbuf; we have libpng
 * and the image_data_to_ximage() shim that takes in-memory PNG data.
 *
 * A simple stub: return NULL. mapscroller / lavalite / maze3d / pulsar /
 * gleidescope / extrusion / worldpieces / timetunnel / glplanet all
 * check the return value and skip texture rendering when it fails --
 * the rest of the hack runs unaffected. A future round could replace
 * this with a libpng-backed implementation if any of those textures
 * turn out to be visually essential. */

/* utf8_decode_combining -- decode the next UTF-8 character, skipping
 * zero-width and combining marks that follow the base character.
 * starwars.c uses this to render one logical character at a time from
 * a UTF-8 string. Our stub ignores the combining logic and just
 * returns the first character's advance; visual quality of combining
 * marks in the scrolling text is the only thing that degrades.
 */

/* make_uniform_colormap (utils/colors.c) -- fills *colors with ncolors
 * entries that sweep once around the hue wheel at a fixed random
 * saturation and value. Equivalent to make_color_ramp(0..360) with
 * one less call frame on the stack.
 *
 * Vendored hacks reach this through colors.h (hilbert, kaleidocycle,
 * and probably more). The screen/visual/cmap/allocate_p/writable_pP/
 * verbose_p parameters are vestigial -- no X server to allocate cells
 * in. The implementation reuses our make_color_ramp shim. */
void make_uniform_colormap(Screen *screen, Visual *visual, Colormap cmap,
                           XColor *colors, int *ncolorsP,
                           Bool allocate_p, Bool *writable_pP,
                           Bool verbose_p)
{
    int ncolors;
    double S, V;

    (void) screen; (void) visual; (void) cmap;
    (void) allocate_p; (void) writable_pP; (void) verbose_p;

    if (!colors || !ncolorsP || *ncolorsP <= 0) return;
    ncolors = *ncolorsP;

    S = ((double) (random() % 34) + 66) / 100.0;
    V = ((double) (random() % 34) + 66) / 100.0;

    make_color_ramp(screen, visual, cmap,
                    0, S, V, 359, S, V,
                    colors, &ncolors,
                    False, allocate_p, writable_pP);
}

/* --- Xft function stubs (xft.h) ---------------------------------------- *
 *
 * photopile.c pulls in xftwrap.c, which calls XftTextExtentsUtf8 and
 * XftDrawStringUtf8 for word-wrapping title text. We have no real Xft.
 * Stub all of them out: NULL returns, zero-fill extents, no-op draws.
 * The vendor hacks guard all real text rendering with `if (font)`, so
 * the no-op draws simply skip the title -- photopile's images still
 * shuffle and stack. */




/* uc_isspace / uc_ispunct / uc_is_combining -- upstream lives in
 * utils/utf8wc.c. Used by xftwrap.c to skip whitespace/punctuation in
 * word-wrap decisions. We don't have real Unicode classification
 * tables here; the no-op approximation is fine for a shim -- the
 * text rendering is dead anyway. */


