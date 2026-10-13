/* SPDX-License-Identifier: Apache-2.0 */
/* gles3_neonspacewar.c — self-playing neon vector rock-shooter.
 *
 * Working name: GenXRockCade (operator-controlled placeholder; rename
 * at the top of this file if it changes). The screensaver plays itself
 * forever: a ship in a wrapping playfield, drifting rocks that split
 * when shot, all rendered as glowing neon vector lines.
 *
 * Mirrors the proven shape of src/gles3_blackhole.c and
 * src/gles3_lavafield.c: one driver .c, three vendor shaders
 * (lines / trail / composite), loaded at runtime from the source
 * tree or from /usr/share/ncz-screensavers/shaders/ (installed
 * via meson install_data). The shader install is mandatory per
 * the operator's "ships broken when cwd is /" precedent (f58ae3a).
 *
 * Per-launch randomisation (ship colour scheme, palette, rock
 * density, starting wave, AI aggression) is seeded from
 * /dev/urandom and printed to stderr in the established [diag]
 * format. NCZ_NEO_SPACEWAR_FIXED_SEED overrides for A/B.
 *
 * The demo AI is the feature — see neonspacewar_ai.h for the
 * threat-triage + lead-targeting + pacing logic. Death is rare
 * but spectacular: the ship shatters into its own line segments.
 */

#define NCZ_NEO_SPACEWAR_NAME  "neonspacewar"
#define NCZ_NEO_SPACEWAR_CLASS "neonspacewar"

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <stddef.h>
#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>
#include "ncz_gl.h"
#include "ncz_hack_shim.h"
#include "ncz_platform.h"
#include "ncz_options.h"
#include "ncz_harness_cfg.h"
#include "ncz_render.h"
#include "nsw_melee.h"
#include "nsw_melee_render.h"

/* ---- options: Melee (the default) or the original rock-field game ---- */
static const ncz_opt_def NSW_OPTS[] = {
 /* name, type, default, min, max, choices, aliases, env_alias, label, description, group */
 {"mode", NCZ_OPT_ENUM, "melee", 0, 0, "melee,classic", "", NULL,
  "Mode", "melee: battles between alien races with their own ships, weapons and special abilities. classic: the original rock field.", "Look"},
 {"preset", NCZ_OPT_STRING, "", 0, 0, "", "", NULL,
  "Scene", "A named set of the options below (see --list-presets); options you set yourself still win.", "Look"},
 {"randomize", NCZ_OPT_ENUM, "auto", 0, 0, "auto,off", "", NULL,
  "Randomize", "auto: every option you or the scene leave unset is drawn from the seed at launch (planet size and gravity, boundary, matchups, palette, glow, trails), so each run differs and the seed reproduces it. off: the defaults.", "Look"},
 {"races", NCZ_OPT_LIST, "needle,armour,ring,insect,crescent,ram,lance", 0, 0, "needle,armour,ring,insect,crescent,ram,lance", "", NULL,
  "Races", "Which races may fight (at least two): the needle (spread shot, blink), the armour block (plasma, shield), the ring (homing darts, mines), the insect (chain lightning, drones), the crescent (boomerang, cloak), the ram (flak, tractor) and the lance (piercing lance, repulse).", "Battles"},
 {"matchups", NCZ_OPT_ENUM, "mix", 0, 0, "mix,duel,teams,ffa", "", NULL,
  "Matchups", "mix: mostly duels, sometimes two against two or three-way. duel, teams or ffa: only that kind (three-way needs three races).", "Battles"},
 {"time-limit", NCZ_OPT_FLOAT, "90", 30, 180, "", "", NULL,
  "Time limit", "Seconds a battle may last; then the side with the most hull left wins.", "Battles"},
 {"gravity", NCZ_OPT_FLOAT, "1", 0, 3, "", "", NULL,
  "Planet gravity", "Planet mass, as a multiple of the default; 0 turns the pull off.", "Arena"},
 {"planet-size", NCZ_OPT_FLOAT, "1", 0.5, 2, "", "", NULL,
  "Planet size", "Planet radius, as a multiple of the default.", "Arena"},
 {"boundary", NCZ_OPT_FLOAT, "1", 0, 3, "", "", NULL,
  "Closing boundary", "How fast the arena closes after 12 s without a hit, as a multiple of the default; 0 never closes it.", "Arena"},
 {"palette", NCZ_OPT_ENUM, "race", 0, 0, "race,ember,ice,phosphor", "", NULL,
  "Palette", "race: each race in its own colour. ember, ice, phosphor: every race drawn from one colour family.", "Look"},
 {"glow", NCZ_OPT_FLOAT, "1", 0, 2, "", "", NULL,
  "Glow", "Strength of the glow around every line; the lines themselves stay sharp.", "Look"},
 {"trails", NCZ_OPT_FLOAT, "1", 0, 2, "", "", NULL,
  "Engine trails", "Strength of the trail each ship leaves.", "Look"},
 {"quality", NCZ_OPT_ENUM, "auto", 0, 0, "auto,low,high", "", NULL,
  "Quality", "auto: fewer particles and a smaller glow buffer on weak GPUs. Hull edges are always drawn at full resolution.", "System"},
 {"camera", NCZ_OPT_ENUM, "auto", 0, 0, "auto,fixed", "", NULL,
  "Camera", "auto: follows the fight, zooming in as the ships close and out as they part, with a punch-in on each kill. fixed: the whole arena, no zoom.", "Look"},
 {"zoom", NCZ_OPT_FLOAT, "0.28", 0.1, 0.4, "", "", NULL,
  "Closest zoom", "How close the auto camera may get, as the largest share of the screen height one ship may fill.", "Look"},
 {"seed", NCZ_OPT_INT, "0", 0, 4294967295.0, "", "", "NCZ_NEO_SPACEWAR_FIXED_SEED",
  "Random seed", "Fixes the matchups and every battle so a run can be reproduced; 0 = random.", "System"},
};
const ncz_opt_def *ncz_hack_options(size_t *n, const char **prefix, const char **group) {
    *n = sizeof NSW_OPTS / sizeof NSW_OPTS[0]; *prefix = "NCZ_NEONSPACEWAR_"; *group = "neonspacewar";
    return NSW_OPTS;
}
/* Melee draws its lines at the output resolution and scales only its glow. */
int ncz_hack_native_scale(void) {
    const ncz_opts *o = ncz_harness_opts();
    const char *m = o ? ncz_opts_get(o, "mode") : "melee";
    return !m || strcmp(m, "classic") != 0;
}

typedef struct {
    NswWorld world;
    NswRender *render;
    double prev;
    unsigned long last_battle;
    double diag_last;
    float capture_dt;
} Melee;
static Melee *g_melee;

static double melee_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
/* Options into the simulation and look. With randomize=auto, every option
 * nobody set (not the user, not the scene) is drawn from the seed, as a
 * coherent set: a bigger planet pulls harder. */
static void melee_options(const ncz_opts *o, uint32_t seed, NswConfig *c, NswLook *l) {
    nsw_config_default(c);
    nsw_look_default(l);
    static const char *const RACE_ID[NSW_RACES] = {"needle", "armour", "ring", "insect", "crescent", "ram", "lance"};
    if (!o) return;
    const char *rl = ncz_opts_get(o, "races");
    c->race_mask = 0;
    for (int r = 0; r < NSW_RACES; r++) {
        size_t n = strlen(RACE_ID[r]);
        for (const char *p = rl; p && *p;) {
            const char *e = strchr(p, ',');
            size_t k = e ? (size_t)(e - p) : strlen(p);
            if (k == n && !strncmp(p, RACE_ID[r], n)) c->race_mask |= 1u << r;
            p = e ? e + 1 : NULL;
        }
    }
    if (__builtin_popcount(c->race_mask) < 2) {
        fprintf(stderr, "[opts] races: fewer than two races chosen; all of them fight\n");
        c->race_mask = (1u << NSW_RACES) - 1u;
    }
    const char *mu = ncz_opts_get(o, "matchups");
    c->matchups = !strcmp(mu, "duel") ? NSW_MATCH_DUEL : !strcmp(mu, "teams") ? NSW_MATCH_TEAMS :
                  !strcmp(mu, "ffa") ? NSW_MATCH_FFA : NSW_MATCH_MIX;
    c->battle_max = (float)ncz_opts_get_float(o, "time-limit");
    c->gravity = (float)ncz_opts_get_float(o, "gravity");
    c->planet = (float)ncz_opts_get_float(o, "planet-size");
    c->contract = (float)ncz_opts_get_float(o, "boundary");
    const char *pal = ncz_opts_get(o, "palette");
    l->palette = !strcmp(pal, "ember") ? NSW_PAL_EMBER : !strcmp(pal, "ice") ? NSW_PAL_ICE :
                 !strcmp(pal, "phosphor") ? NSW_PAL_PHOSPHOR : NSW_PAL_RACE;
    l->glow = (float)ncz_opts_get_float(o, "glow");
    l->trails = (float)ncz_opts_get_float(o, "trails");
    l->cam_fixed = !strcmp(ncz_opts_get(o, "camera"), "fixed");
    l->max_frac = (float)ncz_opts_get_float(o, "zoom");
    if (!strcmp(ncz_opts_get(o, "randomize"), "auto")) {
        uint32_t z = seed * 2654435761u ^ 0xA5A5F00Du;
        if (!z) z = 1;
#define RND() (z ^= z << 13, z ^= z >> 17, z ^= z << 5, (float)(z & 0xffffffu) / 16777216.f)
        /* Every draw is made whether or not it is used, so setting one
         * option never changes what the seed gives the others. */
        float u_planet = RND(), u_grav = RND(), u_bound = RND(), u_match = RND(), u_pal = RND(), u_glow = RND(), u_trail = RND();
        if (!ncz_opts_is_set(o, "planet-size")) c->planet = 0.7f + 0.8f * u_planet;
        if (!ncz_opts_is_set(o, "gravity")) c->gravity = c->planet * (0.8f + 0.4f * u_grav);
        if (!ncz_opts_is_set(o, "boundary")) c->contract = 0.6f + u_bound;
        if (!ncz_opts_is_set(o, "matchups"))
            c->matchups = u_match < 0.70f ? NSW_MATCH_MIX : u_match < 0.85f ? NSW_MATCH_DUEL : u_match < 0.93f ? NSW_MATCH_TEAMS : NSW_MATCH_FFA;
        if (!ncz_opts_is_set(o, "palette"))
            l->palette = u_pal < 0.70f ? NSW_PAL_RACE : u_pal < 0.80f ? NSW_PAL_EMBER : u_pal < 0.90f ? NSW_PAL_ICE : NSW_PAL_PHOSPHOR;
        if (!ncz_opts_is_set(o, "glow")) l->glow = 0.8f + 0.5f * u_glow;
        if (!ncz_opts_is_set(o, "trails")) l->trails = 0.6f + 0.8f * u_trail;
#undef RND
    }
    static const char *const MU[] = {"mix", "duel", "teams", "ffa"}, *const PAL[] = {"race", "ember", "ice", "phosphor"};
    fprintf(stderr, "[diag] neonspacewar melee: races=0x%02x matchups=%s time-limit=%.0f gravity=%.2f planet-size=%.2f "
            "boundary=%.2f palette=%s glow=%.2f trails=%.2f camera=%s zoom=%.2f\n", (unsigned)c->race_mask,
            MU[c->matchups & 3], c->battle_max, c->gravity, c->planet, c->contract, PAL[l->palette & 3], l->glow, l->trails,
            l->cam_fixed ? "fixed" : "auto", l->max_frac);
}

static int melee_init(void) {
    const ncz_opts *o = ncz_harness_opts();
    const char *m = o ? ncz_opts_get(o, "mode") : "melee";
    if (m && !strcmp(m, "classic")) return 0;
    Melee *g = calloc(1, sizeof *g);
    if (!g) return -1;
    const char *q = o ? ncz_opts_get(o, "quality") : "auto";
    int weak = q && !strcmp(q, "low") ? 1 : q && !strcmp(q, "high") ? 0 : !strcmp(ncz_gpu_class_current(), "weak");
    uint32_t seed = 0;
    const char *sd = o ? ncz_opts_get(o, "seed") : NULL;
    if (sd) seed = (uint32_t)strtoul(sd, NULL, 0);
    if (!seed) {
        int f = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        if (f >= 0) { if (read(f, &seed, sizeof seed) != (ssize_t)sizeof seed) seed = 0; close(f); }
        if (!seed) seed = (uint32_t)(melee_now() * 1e6);
    }
    NswConfig cfg;
    NswLook look;
    melee_options(o, seed, &cfg, &look);
    nsw_init_cfg(&g->world, seed, weak, &cfg);
    g->render = nsw_render_create(weak, seed, &look);
    if (!g->render) { free(g); return -1; }
    g->prev = g->diag_last = melee_now();
    const char *cap = getenv("NCZ_NEONSPACEWAR_CAPTURE_FPS");
    if (cap && getenv("NCZ_FRAME_DUMP") && atof(cap) > 0) g->capture_dt = (float)(1.0 / atof(cap));
    g->last_battle = 0;
    g_melee = g;
    fprintf(stderr, "[diag] neonspacewar mode=melee seed=%u quality=%s (NCZ_NEONSPACEWAR_SEED=%u reproduces this run)\n",
            seed, weak ? "light" : "full", seed);
    return 1;
}
static void melee_log_battle(const NswWorld *w) {
    fprintf(stderr, "[diag] neonspacewar battle %d:", w->battle);
    for (int i = 0; i < w->nships; i++) fprintf(stderr, " %s(team %d)", NSW_RACE[w->ship[i].race].name, w->ship[i].team);
    fprintf(stderr, "\n");
}
static void melee_draw(int w, int h) {
    Melee *g = g_melee;
    double tn = melee_now();
    float dt = (float)(tn - g->prev);
    g->prev = tn;
    /* Clip capture (with NCZ_FRAME_DUMP): a fixed frame time, so a slow dump
     * does not change what is recorded. Not a user option. */
    if (g->capture_dt > 0) dt = g->capture_dt;
    unsigned long before = g->world.battles_done;
    nsw_step(&g->world, dt);
    if (g->world.battles_done != before)
        fprintf(stderr, "[diag] neonspacewar battle %d over after %.1f s, winning team %d, state %08x\n", g->world.battle,
                g->world.last_duration, g->world.winner_team, g->world.last_hash);
    if ((unsigned long)g->world.battle != g->last_battle) {
        melee_log_battle(&g->world);
        g->last_battle = (unsigned long)g->world.battle;
    }
    nsw_render_draw(g->render, &g->world, w, h, dt);
    if (tn - g->diag_last > 30.0) {
        fprintf(stderr, "[diag] neonspacewar lines=%d\n", nsw_render_lines(g->render));
        g->diag_last = tn;
    }
}
static void melee_free(void) {
    if (!g_melee) return;
    nsw_render_destroy(g_melee->render);
    free(g_melee);
    g_melee = NULL;
}

#ifdef NCZ_GLES3_BUILD
extern void ncz_harness_die(int code);
#endif

/* ===================================================================== */
/* Math helpers                                                            */
/* ===================================================================== */

typedef struct { float x, y; } V2;
typedef struct { float x, y, z; } V3;

static inline float vclamp(float x, float lo, float hi)
    { return x < lo ? lo : (x > hi ? hi : x); }
static inline float vlerp(float a, float b, float t) { return a + (b - a) * t; }

static inline float vwrap(float x, float lo, float hi) {
    float w = hi - lo;
    while (x <  lo) x += w;
    while (x >= hi) x -= w;
    return x;
}

/* Shortest signed delta on a wrapping axis [lo,hi). */
static inline float vwrap_delta(float from, float to, float lo, float hi) {
    float d = to - from;
    float w = hi - lo;
    while (d >  w * 0.5f) d -= w;
    while (d < -w * 0.5f) d += w;
    return d;
}

static V3 hsl_to_rgb(float h, float s, float l) {
    /* h in [0,1], s,l in [0,1] */
    float c = (1.f - fabsf(2.f * l - 1.f)) * s;
    float hh = h * 6.f;
    float x = c * (1.f - fabsf(fmodf(hh, 2.f) - 1.f));
    float r = 0.f, g = 0.f, b = 0.f;
    if      (hh < 1.f) { r = c; g = x; b = 0; }
    else if (hh < 2.f) { r = x; g = c; b = 0; }
    else if (hh < 3.f) { r = 0; g = c; b = x; }
    else if (hh < 4.f) { r = 0; g = x; b = c; }
    else if (hh < 5.f) { r = x; g = 0; b = c; }
    else               { r = c; g = 0; b = x; }
    float m = l - 0.5f * c;
    V3 v = { r + m, g + m, b + m };
    return v;
}

/* ===================================================================== */
/* Tunables                                                                */
/* ===================================================================== */

#define MAX_ROCKS        64
#define MAX_BULLETS      32
#define MAX_DEBRIS       96
#define MAX_PARTICLES    128
#define MAX_SHOCKWAVES   8
#define MAX_LINES        4096   /* total line segments drawn per frame */
#define TRAIL_LEN        12     /* per-entity history points for trail */

/* ===================================================================== */
/* Game state                                                              */
/* ===================================================================== */

typedef enum { ROCK_LARGE = 0, ROCK_MEDIUM = 1, ROCK_SMALL = 2 } RockTier;

typedef struct {
    V2 pos, vel;
    float radius;          /* collision radius */
    float visual_radius;   /* polygon outline radius */
    RockTier tier;
    int    alive;
    float  rot, rot_v;     /* orientation for polygon vertices */
    int    vertices;       /* number of polygon vertices (irregular) */
    float  shape[12];      /* per-vertex angular offsets + radial jitter */
    float  hue_off;        /* per-rock hue offset for tier colouring */
    V2     trail[TRAIL_LEN];
    int    trail_n;
} Rock;

typedef struct {
    V2 pos, vel;
    int alive;
    float age;
    V2 trail[TRAIL_LEN];
    int  trail_n;
} Bullet;

typedef struct {
    V2 pos, vel;
    float age, life;
    int   alive;
    float hue;
} Debris;

typedef struct {
    V2 pos, vel;
    float age, life;
    int   alive;
    float hue;
} Particle;

typedef struct {
    V2 origin;
    float age, life;
    float strength;
    int   alive;
} Shockwave;

typedef struct {
    V2 pos, vel;
    float heading;            /* radians, 0 = +x */
    float thrust;             /* current engine output (0..1) */
    int   alive;
    float invuln;             /* seconds of post-respawn invulnerability */
    float shoot_cooldown;
    float turn_rate;          /* AI commanded angular velocity */
    float thrust_cmd;         /* AI commanded throttle */
    int   want_to_shoot;      /* AI: 1 if trigger should be down */
    int   dying;              /* death animation in progress */
    float die_t;              /* death time */
    V2   die_frag[8];         /* ship break-apart fragments (line ends) */
    V2   trail[TRAIL_LEN];
    int  trail_n;
    float plume_len;          /* exhaust plume length scalar */
} Ship;

/* A line segment to be drawn into the trail FBO this frame. */
typedef struct {
    float x0, y0, x1, y1;
    float r, g, b;
    float a;
    float width;
} LineSeg;

/* ===================================================================== */
/* Top-level state                                                         */
/* ===================================================================== */

typedef struct {
    /* GLES plumbing */
    GLuint line_prog, trail_prog, comp_prog;
    /* Two ping-pong FBOs for the persistent phosphor trail.
     * draw_decay reads trail_tex[t_read] and writes trail_tex[t_write];
     * draw_lines then writes additively into trail_fbo=t_write. */
    GLuint trail_fbo[2], trail_tex[2];
    int trail_idx;          /* index of the *write* FBO this frame */
    int trail_fbo_w, trail_fbo_h;
    int fb_w, fb_h;         /* visible drawable / composite size */
    GLuint full_vbo;
    GLint  u_line_mvp;
    GLint  u_line_px_to_clip;
    GLint  u_trail_prev;
    GLint  u_trail_decay;
    GLint  u_trail_decay_strength;
    GLint  u_comp_samp;
    GLint  u_comp_resolution;
    GLint  u_comp_time;
    GLint  u_comp_palette_phase;
    GLint  u_comp_chroma;
    GLint  u_comp_bloom;
    GLint  u_comp_shockwave_xy_strength_age;
    GLint  u_comp_shockwave_count;
    GLint  u_comp_wave;
    GLint  u_comp_palette_idx;
    float  mvp[16];           /* aspect-correct ortho */

    /* Game state */
    Ship       ship;
    Rock       rocks[MAX_ROCKS];
    Bullet     bullets[MAX_BULLETS];
    Debris     debris[MAX_DEBRIS];
    Particle   particles[MAX_PARTICLES];
    Shockwave  shocks[MAX_SHOCKWAVES];
    LineSeg    lines[MAX_LINES];
    int        line_n;

    /* Per-launch knobs */
    float palette_idx;
    float palette_phase;
    float palette_rate;
    float density;
    float aggression;
    float ship_hue;
    float plume_hue;
    int   wave;
    int   rocks_alive_this_wave;
    int   rocks_spawned_this_wave;
    int   bullets_fired;
    int   rocks_killed;
    int   deaths;
    double started;
    double wave_started;
    float wave_duration;
} State;

/* ===================================================================== */
/* Forward decls                                                           */
/* ===================================================================== */

static void game_init(State *st);
static void game_step(State *st, float dt);
static void game_render(State *st);
static void ai_update(State *st, float dt);
static void physics_step(State *st, float dt);
static void collision_step(State *st);
static void waves_update(State *st, float dt);
static void spawn_wave(State *st);
static void split_rock(State *st, int idx);
static void kill_ship(State *st);
static void emit_shockwave(State *st, V2 p, float strength);
static void emit_debris(State *st, V2 p, V2 impulse, int count, float hue);
static void emit_particles(State *st, V2 p, V2 impulse, int count, float hue);
static void push_line(State *st, V2 a, V2 b, float r, float g, float bl,
                      float alpha, float width);
static void push_ship_outline(State *st);
static void push_rock_outline(State *st, int idx);
static void push_bullet_outline(State *st, int idx);
static void push_debris_outline(State *st, int idx);
static void push_particles_outline(State *st);
static void draw_trail_decay(State *st);
static void draw_lines(State *st);
static void draw_composite(State *st);

#include "neonspacewar_ai.h"

/* ===================================================================== */
/* Time + RNG                                                              */
/* ===================================================================== */

static double now_monotonic(void) {
    return ncz_now();
}

/* Session seed is computed once at init and threaded through every
 * subsequent rnd() call. Per-launch randomisation derives from
 * ncz_seed() (entropy from /dev/urandom, with a fallback) unless
 * NCZ_NEO_SPACEWAR_FIXED_SEED overrides (for A/B testing). All in-
 * session randomness uses the same xorshift LCG state so successive
 * calls produce uncorrelated streams.
 *
 * Pattern: game_init() calls session_seed() once, stores the
 * result, and threads it through. spawn_rock / emit_debris etc
 * step the LCG from a snapshot of the previous step, so each
 * subsystem gets its own uncorrelated sub-stream. */
static uint32_t g_session_seed = 0;

static uint32_t session_seed(void) {
    const char *override = getenv("NCZ_NEO_SPACEWAR_FIXED_SEED");
    if (override && *override) {
        uint32_t s = (uint32_t)strtoul(override, NULL, 10);
        if (s != 0) return s;
    }
    uint32_t s = ncz_seed();
    if (s == 0) s = 1;
    return s;
}

/* Step the session seed's LCG. Always non-zero. */
static uint32_t seed_rng(void) {
    if (g_session_seed == 0) {
        g_session_seed = session_seed();
        return g_session_seed;
    }
    g_session_seed ^= g_session_seed << 13;
    g_session_seed ^= g_session_seed >> 17;
    g_session_seed ^= g_session_seed << 5;
    if (g_session_seed == 0) g_session_seed = 1;
    return g_session_seed;
}

static float rnd01(uint32_t *s) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return (float)(*s & 0x00ffffffu) / 16777215.0f;
}
static float rnd(uint32_t *s, float a, float b) {
    return a + (b - a) * rnd01(s);
}
static int rnd_int(uint32_t *s, int a, int b) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return a + (int)(*s % (uint32_t)(b - a + 1));
}

/* ===================================================================== */
/* Shader loading + compile                                                */
/* ===================================================================== */

static char *load_shader_text(const char *basename) {
    /* 1) absolute installed-asset path via the platform abstraction. */
    char abs_path[512];
    if (ncz_asset_path(basename, abs_path, sizeof abs_path)) {
        FILE *f = fopen(abs_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
            char *b = malloc((size_t)n + 1);
            if (b && fread(b, 1, (size_t)n, f) == (size_t)n) {
                b[n] = 0; fclose(f);
                ncz_log_diag("neonspacewar shader=%s", abs_path);
                return b;
            }
            free(b); fclose(f);
        }
    }
    /* 2) dev build fallback -- look under vendor/neonspacewar/
     *    relative to cwd (or one or two dirs up). */
    char path[256];
    const char *prefixes[] = {
        "vendor/neonspacewar/",
        "../vendor/neonspacewar/",
        "../../vendor/neonspacewar/",
    };
    for (unsigned i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s%s", prefixes[i], basename);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
        char *b = malloc((size_t)n + 1);
        if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) {
            free(b); fclose(f); return NULL;
        }
        fclose(f); b[n] = 0;
        ncz_log_diag("neonspacewar shader=%s", path);
        return b;
    }
    ncz_log_diag("neonspacewar cannot locate %s", basename);
    return NULL;
}

static GLuint compile_shader(GLenum t, const char *src, const char *tag) {
    GLuint s = glCreateShader(t);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "neonspacewar %s compile: %s\n", tag, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(GLuint vs, GLuint fs, const char *tag) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_color");
    glBindAttribLocation(p, 2, "a_alpha");
    glBindAttribLocation(p, 3, "a_width");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "neonspacewar %s link: %s\n", tag, log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static GLuint build_program(const char *tag, const char *vs_src,
                             const char *fs_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src, tag);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src, tag);
    if (!vs || !fs) { ncz_harness_die(1); return 0; }
    GLuint p = link_program(vs, fs, tag);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!p) ncz_harness_die(1);
    return p;
}

/* ===================================================================== */
/* FBO + geometry                                                          */
/* ===================================================================== */

static int create_fbo(int w, int h, GLuint *out_tex, GLuint *out_fbo) {
    glGenTextures(1, out_tex);
    glBindTexture(GL_TEXTURE_2D, *out_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, out_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, *out_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, *out_tex, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    int rv = (status == GL_FRAMEBUFFER_COMPLETE) ? 0 : -1;
    if (rv < 0)
        fprintf(stderr, "neonspacewar: FBO incomplete 0x%x\n",
                (unsigned)status);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return rv;
}

static void clear_fbo(GLuint fbo, float r, float g, float b, float a) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static const float kFullscreenQuad[] = {
    -1.f, -1.f,  1.f, -1.f, -1.f,  1.f,
    -1.f,  1.f,  1.f, -1.f,  1.f,  1.f,
};

static void setup_geometry_vbo(GLuint *vbo) {
    glGenBuffers(1, vbo);
    glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof kFullscreenQuad,
                 kFullscreenQuad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* Dynamic VBO for line segments. Each segment is expanded to two
 * triangles (6 verts) at draw time: a perpendicular extrusion by
 * +-a_width*u_px_to_clip in clip space, which gives sub-pixel
 * control without needing a CPU-side polyline expansion.
 *
 * Layout per-vertex (LineVert):
 *   x, y            world-space endpoint
 *   ox, oy          world-space other endpoint of the segment
 *   r, g, b         line colour
 *   a               alpha multiplier
 *   w               half-thickness in pixels
 *   s               side: -1 or +1
 */
typedef struct {
    float x,  y;
    float ox, oy;
    float r, g, b;
    float a;
    float w;
    float s;
} LineVert;
#define MAX_LINE_VERTS (MAX_LINES * 6)
static LineVert g_lineverts[MAX_LINE_VERTS];
static GLuint g_line_vbo;

static void setup_line_vbo(void) {
    glGenBuffers(1, &g_line_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_line_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof g_lineverts, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* ===================================================================== */
/* Line push helpers                                                       */
/* ===================================================================== */

static void push_line(State *st, V2 a, V2 b, float r, float g, float bl,
                      float alpha, float width) {
    if (st->line_n >= MAX_LINES) return;
    LineSeg *L = &st->lines[st->line_n++];
    L->x0 = a.x; L->y0 = a.y; L->x1 = b.x; L->y1 = b.y;
    L->r = r; L->g = g; L->b = bl;
    L->a = alpha;
    L->width = width;
}

/* ===================================================================== */
/* Draw passes                                                             */
/* ===================================================================== */

/* ===================================================================== */
/* Trail ping-pong: read previous frame, multiply by per-channel decay,    */
/* write to the other FBO. Line draws in draw_lines() then add into the   */
/* *write* FBO additively afterwards.                                      */
/* ===================================================================== */
static void draw_trail_decay(State *st) {
    int write = st->trail_idx;
    int read  = 1 - st->trail_idx;
    glUseProgram(st->trail_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, st->trail_tex[read]);
    glUniform1i(st->u_trail_prev, 0);
    /* Per-channel phosphor decay: red fades faster than blue. */
    glUniform3f(st->u_trail_decay, 0.94f, 0.965f, 0.992f);
    glUniform1f(st->u_trail_decay_strength, 1.0f);

    glBindFramebuffer(GL_FRAMEBUFFER, st->trail_fbo[write]);
    glViewport(0, 0, st->trail_fbo_w, st->trail_fbo_h);

    /* Plain copy: dst = sampled value (no blend). The fade shader
     * itself applies the per-channel decay. */
    glDisable(GL_BLEND);

    glBindBuffer(GL_ARRAY_BUFFER, st->full_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glEnable(GL_BLEND);
    glUseProgram(0);
}

static void draw_lines(State *st) {
    if (st->line_n == 0) return;
    /* Expand each segment into two triangles (6 verts). Each triangle's
     * vertex pair along the segment shares an endpoint. The vertex
     * shader extrudes them by +-a_width*u_px_to_clip along the segment's
     * perpendicular. Two triangles per segment drawn as GL_TRIANGLES. */
    int vcount = 0;
    int max_v = MAX_LINE_VERTS;
    for (int i = 0; i < st->line_n; i++) {
        const LineSeg *L = &st->lines[i];
        if (vcount + 6 > max_v) break;
        LineVert *verts = &g_lineverts[vcount];
        /* Endpoint A (x0,y0) - both sides */
        verts[0].x = L->x0; verts[0].y = L->y0;
        verts[1].x = L->x0; verts[1].y = L->y0;
        /* Endpoint B (x1,y1) - both sides */
        verts[2].x = L->x1; verts[2].y = L->y1;
        verts[3].x = L->x1; verts[3].y = L->y1;
        /* 'other' endpoint is the opposite corner of this segment */
        verts[0].ox = L->x1; verts[0].oy = L->y1;
        verts[1].ox = L->x1; verts[1].oy = L->y1;
        verts[2].ox = L->x0; verts[2].oy = L->y0;
        verts[3].ox = L->x0; verts[3].oy = L->y0;
        /* colours / alpha / width are shared across the 4 extruded
         * verts; sides 0,2 = -1 and 1,3 = +1 (the perpendicular
         * normal flips sign on the second triangle). */
        float cols[4][3] = {
            {L->r, L->g, L->b},
            {L->r, L->g, L->b},
            {L->r, L->g, L->b},
            {L->r, L->g, L->b},
        };
        float sides[4]  = {-1.0f, 1.0f, -1.0f, 1.0f};
        float alphas[4] = {L->a, L->a, L->a, L->a};
        float widths[4] = {L->width, L->width, L->width, L->width};
        for (int k = 0; k < 4; k++) {
            verts[k].r = cols[k][0];
            verts[k].g = cols[k][1];
            verts[k].b = cols[k][2];
            verts[k].a = alphas[k];
            verts[k].w = widths[k];
            verts[k].s = sides[k];
        }
        /* Two-triangle quad (indexed implicitly): 0,1,2 and 1,3,2 */
        LineVert tri0[3] = {verts[0], verts[1], verts[2]};
        LineVert tri1[3] = {verts[1], verts[3], verts[2]};
        memcpy(&verts[0], tri0, sizeof tri0);
        memcpy(&verts[3], tri1, sizeof tri1);
        vcount += 6;
    }
    if (vcount == 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, g_line_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(vcount * sizeof (LineVert)),
                    g_lineverts);

    glBindFramebuffer(GL_FRAMEBUFFER, st->trail_fbo[st->trail_idx]);
    glViewport(0, 0, st->trail_fbo_w, st->trail_fbo_h);
    glUseProgram(st->line_prog);
    glUniformMatrix4fv(st->u_line_mvp, 1, GL_FALSE, st->mvp);
    /* px_to_clip converts 1 pixel into NDC units. With the aspect-
     * corrected ortho both X and Y span 2.0 NDC units; using
     * min(w,h) gives isotropic stroke widths. */
    float min_dim = (float)(st->trail_fbo_w < st->trail_fbo_h
                             ? st->trail_fbo_w
                             : st->trail_fbo_h);
    float px_to_clip = 2.0f / min_dim;
    glUniform1f(st->u_line_px_to_clip, px_to_clip);

    glBlendFunc(GL_ONE, GL_ONE);  /* additive */

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof (LineVert),
                          (void *)offsetof(LineVert, x));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof (LineVert),
                          (void *)offsetof(LineVert, ox));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof (LineVert),
                          (void *)offsetof(LineVert, r));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof (LineVert),
                          (void *)offsetof(LineVert, a));
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof (LineVert),
                          (void *)offsetof(LineVert, w));
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof (LineVert),
                          (void *)offsetof(LineVert, s));

    glDrawArrays(GL_TRIANGLES, 0, vcount);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);
    glDisableVertexAttribArray(5);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void draw_composite(State *st) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, st->fb_w, st->fb_h);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Composite reads the just-written trail FBO (this frame's source
     * for the visible composite output). The ping-pong swap happens
     * at frame end, so the *next* frame's decay reads this one. */
    glUseProgram(st->comp_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, st->trail_tex[st->trail_idx]);
    glUniform1i(st->u_comp_samp, 0);
    glUniform2f(st->u_comp_resolution, (float)st->fb_w, (float)st->fb_h);
    glUniform1f(st->u_comp_time, (float)(now_monotonic() - st->started));
    glUniform1f(st->u_comp_palette_phase, st->palette_phase);
    glUniform1f(st->u_comp_palette_idx, st->palette_idx);
    float action = (float)st->rocks_alive_this_wave / 12.f;
    if (action > 1.f) action = 1.f;
    glUniform1f(st->u_comp_chroma,
                vlerp(0.0008f, 0.006f, action)
                * (1.f + 0.4f * (float)st->wave / 10.f));
    glUniform1f(st->u_comp_bloom,
                vlerp(0.6f, 1.1f, (float)st->wave / 12.f));
    glUniform1f(st->u_comp_wave, (float)st->wave);

    int n = 0;
    float pack[8 * 4] = {0};
    for (int i = 0; i < MAX_SHOCKWAVES && n < 8; i++) {
        if (!st->shocks[i].alive) continue;
        Shockwave *s = &st->shocks[i];
        pack[n*4 + 0] = s->origin.x;
        pack[n*4 + 1] = s->origin.y;
        pack[n*4 + 2] = s->age / s->life;
        pack[n*4 + 3] = s->strength;
        n++;
    }
    glUniform4fv(st->u_comp_shockwave_xy_strength_age, 8, pack);
    glUniform1i(st->u_comp_shockwave_count, n);

    glBlendFunc(GL_ONE, GL_ZERO);  /* opaque composite */

    glBindBuffer(GL_ARRAY_BUFFER, st->full_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

/* ===================================================================== */
/* Game init                                                               */
/* ===================================================================== */

static void reset_ship(Ship *s) {
    s->pos.x = 0.f; s->pos.y = 0.f;
    s->vel.x = 0.f; s->vel.y = 0.f;
    s->heading = 0.f;
    s->thrust = 0.f;
    s->alive = 1;
    s->invuln = 4.0f;
    s->shoot_cooldown = 0.f;
    s->turn_rate = 0.f;
    s->thrust_cmd = 0.f;
    s->want_to_shoot = 0;
    s->dying = 0;
    s->die_t = 0.f;
    for (int i = 0; i < 8; i++) {
        s->die_frag[i].x = 0; s->die_frag[i].y = 0;
    }
    s->trail_n = 0;
    s->plume_len = 0.f;
}

static void game_init(State *st) {
    /* Wipe just the per-launch fields, not the GL plumbing we just
     * set up. The plumbing fields live at the top of the struct
     * (line_prog, trail_prog, comp_prog, ...); game state starts
     * at the `ship` field. */
    memset((char*)st + offsetof(State, ship), 0,
           sizeof (State) - offsetof(State, ship));

    /* Reset the global LCG state so this launch produces a fresh,
     * uncorrelated stream. Without this, a second launch in the
     * same process would inherit the state from the first. */
    g_session_seed = 0;

    /* Per-launch randomisation. Capture the session seed first
     * (this is what NCZ_NEO_SPACEWAR_FIXED_SEED / /dev/urandom
     * produces) before the LCG gets stepped by subsequent rnd()
     * calls. */
    uint32_t session = seed_rng();
    uint32_t z = session;
    int pal_n = 6;
    st->palette_idx      = (float)(rnd_int(&z, 0, pal_n - 1));
    st->palette_phase    = rnd(&z, 0.f, 1.f);
    st->palette_rate     = rnd(&z, 0.020f, 0.060f);
    st->density          = rnd(&z, 0.85f, 1.30f);
    st->aggression       = rnd(&z, 0.7f, 1.3f);
    st->ship_hue         = rnd(&z, 0.f, 1.f);
    st->plume_hue        = fmodf(st->ship_hue + 0.45f, 1.f);
    st->wave             = rnd_int(&z, 1, 3);
    st->wave_started     = now_monotonic();
    st->wave_duration    = 22.f;
    st->started          = now_monotonic();

    reset_ship(&st->ship);
    spawn_wave(st);

    fprintf(stderr,
        "[diag] neonspacewar seed=%u palette=%d palette_phase=%.4f "
        "palette_rate=%.5f density=%.3f start_wave=%d aggression=%.3f "
        "ship_hue=%.3f plume_hue=%.3f GL=%s\n",
        session, (int)st->palette_idx, st->palette_phase, st->palette_rate,
        st->density, st->wave, st->aggression,
        st->ship_hue, st->plume_hue, (const char *)glGetString(GL_VERSION));
}

/* ===================================================================== */
/* Rock spawning                                                           */
/* ===================================================================== */

static int find_free_rock(State *st) {
    for (int i = 0; i < MAX_ROCKS; i++)
        if (!st->rocks[i].alive) return i;
    return -1;
}

static void spawn_rock(State *st, V2 pos, V2 vel, RockTier tier) {
    int idx = find_free_rock(st);
    if (idx < 0) return;
    Rock *r = &st->rocks[idx];
    uint32_t z = seed_rng() ^ (uint32_t)(idx * 7919u + st->wave * 31u);
    memset(r, 0, sizeof *r);
    r->pos = pos;
    r->vel = vel;
    r->tier = tier;
    r->alive = 1;
    r->rot = rnd(&z, 0.f, 2.f * (float)M_PI);
    r->rot_v = rnd(&z, -0.6f, 0.6f);
    r->vertices = rnd_int(&z, 8, 12);
    r->hue_off = rnd(&z, -0.06f, 0.06f);
    for (int i = 0; i < r->vertices; i++) {
        r->shape[i] = rnd(&z, 0.65f, 1.05f);
    }
    switch (tier) {
        case ROCK_LARGE:  r->radius = 0.22f; r->visual_radius = 0.30f; break;
        case ROCK_MEDIUM: r->radius = 0.13f; r->visual_radius = 0.18f; break;
        case ROCK_SMALL:  r->radius = 0.06f; r->visual_radius = 0.09f; break;
    }
    r->trail_n = 0;
}

static int count_alive_rocks(State *st) {
    int n = 0;
    for (int i = 0; i < MAX_ROCKS; i++) if (st->rocks[i].alive) n++;
    return n;
}

static int count_alive_rocks(State *st);

static void spawn_wave(State *st) {
    int n_large = 2 + st->wave / 2;
    n_large = (int)((float)n_large * st->density);
    if (n_large > 10) n_large = 10;
    if (n_large < 2) n_large = 2;

    uint32_t wz = seed_rng();
    for (int i = 0; i < n_large; i++) {
        float a = ((float)i / (float)n_large + rnd01(&wz) * 0.3f)
                  * 2.f * (float)M_PI;
        V2 pos = { cosf(a) * 0.85f, sinf(a) * 0.85f };
        float sp = 0.15f + 0.04f * (float)st->wave;
        V2 vel = { -cosf(a) * sp, -sinf(a) * sp };
        spawn_rock(st, pos, vel, ROCK_LARGE);
    }
    st->wave_started = now_monotonic();
    st->wave_duration = 35.f + (float)st->wave * 2.5f;
    st->rocks_spawned_this_wave = n_large;
    st->rocks_alive_this_wave = n_large;
    fprintf(stderr, "[diag] neonspacewar wave=%d spawned=%d duration=%.1f\n",
            st->wave, n_large, st->wave_duration);
}

static void waves_update(State *st, float dt) {
    int alive = count_alive_rocks(st);
    st->rocks_alive_this_wave = alive;
    float wave_t = (float)(now_monotonic() - st->wave_started);

    if (alive == 0 || wave_t > st->wave_duration) {
        if (alive > 0 && wave_t > st->wave_duration) {
            /* Time-out: nuke remaining rocks for the next wave. */
            uint32_t z = seed_rng();
            for (int i = 0; i < MAX_ROCKS; i++)
                if (st->rocks[i].alive) {
                    emit_shockwave(st, st->rocks[i].pos, 0.4f);
                    V2 imp = { rnd(&z, -0.4f, 0.4f), rnd(&z, -0.4f, 0.4f) };
                    emit_debris(st, st->rocks[i].pos, imp, 12, 0.0f);
                    st->rocks[i].alive = 0;
                }
        }
        st->wave++;
        spawn_wave(st);
    }
}

/* ===================================================================== */
/* Collisions                                                              */
/* ===================================================================== */

static void split_rock(State *st, int idx) {
    Rock *r = &st->rocks[idx];
    if (!r->alive) return;
    V2 p = r->pos;
    RockTier next = (RockTier)((int)r->tier + 1);
    if ((int)next > (int)ROCK_SMALL) {
        emit_shockwave(st, p, 0.5f);
        emit_debris(st, p, (V2){0,0}, 14, r->hue_off);
        emit_particles(st, p, (V2){0,0}, 10, r->hue_off);
        r->alive = 0;
        st->rocks_killed++;
        return;
    }
    uint32_t z = seed_rng() ^ (uint32_t)(idx * 31u + st->wave * 17u);
    /* Slower split speed so children don't immediately threaten the
     * ship. Original 0.35-0.45 was too aggressive — children
     * would reach the centre in ~2s. */
    float speed = 0.18f + 0.06f * (float)(int)next;
    for (int i = 0; i < 2; i++) {
        float a = rnd(&z, 0.f, 2.f * (float)M_PI);
        V2 v = { cosf(a) * speed, sinf(a) * speed };
        spawn_rock(st, p, v, next);
    }
    emit_shockwave(st, p, 0.6f);
    emit_debris(st, p, (V2){0,0}, 14, r->hue_off);
    emit_particles(st, p, (V2){0,0}, 12, r->hue_off);
    r->alive = 0;
    st->rocks_killed++;
}

static void collision_step(State *st) {
    for (int b = 0; b < MAX_BULLETS; b++) {
        Bullet *bl = &st->bullets[b];
        if (!bl->alive) continue;
        for (int r = 0; r < MAX_ROCKS; r++) {
            Rock *rk = &st->rocks[r];
            if (!rk->alive) continue;
            float dx = bl->pos.x - rk->pos.x;
            float dy = bl->pos.y - rk->pos.y;
            float d2 = dx * dx + dy * dy;
            float sum = 0.012f + rk->radius;
            if (d2 < sum * sum) {
                bl->alive = 0;
                split_rock(st, r);
                break;
            }
        }
    }
    if (st->ship.alive && st->ship.invuln <= 0.f && !st->ship.dying) {
        for (int r = 0; r < MAX_ROCKS; r++) {
            Rock *rk = &st->rocks[r];
            if (!rk->alive) continue;
            float dx = st->ship.pos.x - rk->pos.x;
            float dy = st->ship.pos.y - rk->pos.y;
            float d2 = dx * dx + dy * dy;
            float sum = 0.025f + rk->radius;
            if (d2 < sum * sum) {
                kill_ship(st);
                break;
            }
        }
    }
}

/* ===================================================================== */
/* Death + FX                                                              */
/* ===================================================================== */

static void kill_ship(State *st) {
    if (!st->ship.alive || st->ship.dying) return;
    st->ship.dying = 1;
    st->ship.die_t = 0.f;
    st->ship.thrust_cmd = 0.f;
    st->ship.thrust = 0.f;
    Ship *s = &st->ship;
    for (int i = 0; i < 8; i++) {
        float a = (float)i / 8.f * 2.f * (float)M_PI + s->heading;
        s->die_frag[i].x = s->pos.x + cosf(a) * 0.025f;
        s->die_frag[i].y = s->pos.y + sinf(a) * 0.025f;
    }
    emit_shockwave(st, s->pos, 1.0f);
    emit_debris(st, s->pos, (V2){0,0}, 28, 0.0f);
    emit_particles(st, s->pos, (V2){0,0}, 28, 0.5f);
    st->deaths++;
}

static void emit_shockwave(State *st, V2 p, float strength) {
    for (int i = 0; i < MAX_SHOCKWAVES; i++) {
        if (st->shocks[i].alive) continue;
        st->shocks[i].alive = 1;
        st->shocks[i].origin = p;
        st->shocks[i].age = 0.f;
        st->shocks[i].life = 0.7f;
        st->shocks[i].strength = strength;
        return;
    }
}

static void emit_debris(State *st, V2 p, V2 impulse, int count, float hue) {
    uint32_t z = seed_rng();
    for (int n = 0; n < count; n++) {
        int slot = -1;
        for (int i = 0; i < MAX_DEBRIS; i++)
            if (!st->debris[i].alive) { slot = i; break; }
        if (slot < 0) return;
        Debris *d = &st->debris[slot];
        d->alive = 1;
        d->pos = p;
        float a = rnd(&z, 0.f, 2.f * (float)M_PI);
        float s = rnd(&z, 0.05f, 0.25f);
        d->vel.x = impulse.x + cosf(a) * s;
        d->vel.y = impulse.y + sinf(a) * s;
        d->age = 0.f;
        d->life = rnd(&z, 0.8f, 2.0f);
        d->hue = hue + rnd(&z, -0.05f, 0.05f);
    }
}

static void emit_particles(State *st, V2 p, V2 impulse, int count, float hue) {
    uint32_t z = seed_rng();
    for (int n = 0; n < count; n++) {
        int slot = -1;
        for (int i = 0; i < MAX_PARTICLES; i++)
            if (!st->particles[i].alive) { slot = i; break; }
        if (slot < 0) return;
        Particle *pa = &st->particles[slot];
        pa->alive = 1;
        pa->pos = p;
        float a = rnd(&z, 0.f, 2.f * (float)M_PI);
        float s = rnd(&z, 0.1f, 0.6f);
        pa->vel.x = impulse.x + cosf(a) * s;
        pa->vel.y = impulse.y + sinf(a) * s;
        pa->age = 0.f;
        pa->life = rnd(&z, 0.3f, 0.9f);
        pa->hue = hue + rnd(&z, -0.1f, 0.1f);
    }
}

/* ===================================================================== */
/* Bullets + physics                                                       */
/* ===================================================================== */

static void fire_bullet(State *st) {
    if (st->ship.shoot_cooldown > 0.f) return;
    if (!st->ship.alive || st->ship.dying) return;
    if (!st->ship.want_to_shoot) return;
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (st->bullets[i].alive) continue;
        Bullet *b = &st->bullets[i];
        b->alive = 1;
        b->pos = st->ship.pos;
        float speed = 1.0f;
        b->vel.x = cosf(st->ship.heading) * speed;
        b->vel.y = sinf(st->ship.heading) * speed;
        b->age = 0.f;
        b->trail_n = 0;
        st->bullets_fired++;
        st->ship.shoot_cooldown = 0.18f;
        st->ship.vel.x -= cosf(st->ship.heading) * 0.02f;
        st->ship.vel.y -= sinf(st->ship.heading) * 0.02f;
        return;
    }
}

static void physics_step(State *st, float dt) {
    Ship *s = &st->ship;
    if (s->alive && !s->dying) {
        s->heading += s->turn_rate * dt;
        s->thrust = vlerp(s->thrust, s->thrust_cmd, dt * 4.f);
        s->plume_len = s->plume_len * 0.85f + s->thrust * 0.15f;
        float ax = cosf(s->heading) * s->thrust * 0.9f;
        float ay = sinf(s->heading) * s->thrust * 0.9f;
        s->vel.x += ax * dt;
        s->vel.y += ay * dt;
        s->vel.x *= (1.f - 0.6f * dt);
        s->vel.y *= (1.f - 0.6f * dt);
        float sp = sqrtf(s->vel.x * s->vel.x + s->vel.y * s->vel.y);
        float maxsp = 0.55f;
        if (sp > maxsp) {
            s->vel.x *= maxsp / sp;
            s->vel.y *= maxsp / sp;
        }
        s->pos.x += s->vel.x * dt;
        s->pos.y += s->vel.y * dt;
        s->pos.x = vwrap(s->pos.x, -1.05f, 1.05f);
        s->pos.y = vwrap(s->pos.y, -1.05f, 1.05f);
        s->invuln -= dt;
        s->shoot_cooldown -= dt;
    }
    if (s->dying) {
        s->die_t += dt;
        if (s->die_t > 1.4f) {
            s->dying = 0;
            s->alive = 1;
            s->pos.x = 0.f; s->pos.y = 0.f;
            s->vel.x = 0.f; s->vel.y = 0.f;
            s->heading = 0.f;
            s->thrust = 0.f;
            s->invuln = 4.0f;
            s->want_to_shoot = 0;
            s->shoot_cooldown = 0.f;
            s->trail_n = 0;
            /* Nuke any rocks within 0.45 units of the respawn
             * point so the ship gets a brief safe zone. */
            uint32_t rz = seed_rng();
            for (int i = 0; i < MAX_ROCKS; i++) {
                Rock *r = &st->rocks[i];
                if (!r->alive) continue;
                float dx = r->pos.x - 0.f;
                float dy = r->pos.y - 0.f;
                if (dx*dx + dy*dy < 0.20f) {
                    emit_shockwave(st, r->pos, 0.4f);
                    V2 imp = { rnd(&rz, -0.3f, 0.3f), rnd(&rz, -0.3f, 0.3f) };
                    emit_debris(st, r->pos, imp, 8, 0.0f);
                    r->alive = 0;
                    st->rocks_killed++;
                }
            }
        }
    }

    if (s->alive && !s->dying) {
        for (int i = TRAIL_LEN - 1; i > 0; i--)
            s->trail[i] = s->trail[i - 1];
        s->trail[0] = s->pos;
        if (s->trail_n < TRAIL_LEN) s->trail_n++;
    }

    for (int i = 0; i < MAX_ROCKS; i++) {
        Rock *r = &st->rocks[i];
        if (!r->alive) continue;
        r->pos.x += r->vel.x * dt;
        r->pos.y += r->vel.y * dt;
        r->pos.x = vwrap(r->pos.x, -1.05f, 1.05f);
        r->pos.y = vwrap(r->pos.y, -1.05f, 1.05f);
        r->rot += r->rot_v * dt;
        for (int j = TRAIL_LEN - 1; j > 0; j--)
            r->trail[j] = r->trail[j - 1];
        r->trail[0] = r->pos;
        if (r->trail_n < TRAIL_LEN) r->trail_n++;
    }

    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &st->bullets[i];
        if (!b->alive) continue;
        b->pos.x += b->vel.x * dt;
        b->pos.y += b->vel.y * dt;
        b->pos.x = vwrap(b->pos.x, -1.05f, 1.05f);
        b->pos.y = vwrap(b->pos.y, -1.05f, 1.05f);
        b->age += dt;
        for (int j = TRAIL_LEN - 1; j > 0; j--)
            b->trail[j] = b->trail[j - 1];
        b->trail[0] = b->pos;
        if (b->trail_n < TRAIL_LEN) b->trail_n++;
        if (b->age > 1.2f) b->alive = 0;
    }

    for (int i = 0; i < MAX_DEBRIS; i++) {
        Debris *d = &st->debris[i];
        if (!d->alive) continue;
        d->pos.x += d->vel.x * dt;
        d->pos.y += d->vel.y * dt;
        d->pos.x = vwrap(d->pos.x, -1.05f, 1.05f);
        d->pos.y = vwrap(d->pos.y, -1.05f, 1.05f);
        d->vel.x *= 0.985f;
        d->vel.y *= 0.985f;
        d->age += dt;
        if (d->age > d->life) d->alive = 0;
    }

    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &st->particles[i];
        if (!p->alive) continue;
        p->pos.x += p->vel.x * dt;
        p->pos.y += p->vel.y * dt;
        p->pos.x = vwrap(p->pos.x, -1.05f, 1.05f);
        p->pos.y = vwrap(p->pos.y, -1.05f, 1.05f);
        p->vel.x *= 0.95f;
        p->vel.y *= 0.95f;
        p->age += dt;
        if (p->age > p->life) p->alive = 0;
    }

    for (int i = 0; i < MAX_SHOCKWAVES; i++) {
        Shockwave *sh = &st->shocks[i];
        if (!sh->alive) continue;
        sh->age += dt;
        if (sh->age > sh->life) sh->alive = 0;
    }
}

/* ===================================================================== */
/* Outline push                                                            */
/* ===================================================================== */

static V3 colour_for_rock_tier(int tier, float hue_off) {
    float base;
    switch (tier) {
        case ROCK_LARGE:  base = 0.02f; break;
        case ROCK_MEDIUM: base = 0.30f; break;
        case ROCK_SMALL:  base = 0.85f; break;
        default:          base = 0.f;    break;
    }
    float h = fmodf(base + hue_off + 1.f, 1.f);
    return hsl_to_rgb(h, 0.85f, 0.55f);
}

static V3 colour_for_bullet(float age) {
    float t = vclamp(age / 1.2f, 0.f, 1.f);
    V3 c1 = { 1.0f, 1.0f, 1.0f };
    V3 c2 = hsl_to_rgb(0.55f, 1.0f, 0.65f);
    V3 out = {
        vlerp(c1.x, c2.x, t),
        vlerp(c1.y, c2.y, t),
        vlerp(c1.z, c2.z, t),
    };
    return out;
}

static void push_ship_outline(State *st) {
    Ship *s = &st->ship;
    if (!s->alive && !s->dying) return;

    if (s->dying) {
        float t = s->die_t;
        if (t > 1.4f) t = 1.4f;
        float fade = 1.f - t / 1.4f;
        V3 c = hsl_to_rgb(st->ship_hue, 1.f, 0.7f);
        for (int i = 0; i < 8; i++) {
            V2 a = s->die_frag[i];
            float ang = (float)i / 8.f * 2.f * (float)M_PI + t * 8.f;
            float r = 0.025f + t * 0.20f;
            V2 b = { s->pos.x + cosf(ang) * r,
                     s->pos.y + sinf(ang) * r };
            push_line(st, a, b,
                      c.x * fade, c.y * fade, c.z * fade,
                      0.9f * fade, 2.5f);
        }
        return;
    }

    float h = s->heading;
    V3 ship_c = hsl_to_rgb(st->ship_hue, 1.f, 0.65f);
    V3 ship_bright = hsl_to_rgb(st->ship_hue, 0.3f, 0.95f);
    V2 nose  = { s->pos.x + cosf(h) * 0.06f,
                 s->pos.y + sinf(h) * 0.06f };
    V2 rL    = { s->pos.x + cosf(h + 2.5f) * 0.045f,
                 s->pos.y + sinf(h + 2.5f) * 0.045f };
    V2 rR    = { s->pos.x + cosf(h - 2.5f) * 0.045f,
                 s->pos.y + sinf(h - 2.5f) * 0.045f };
    V2 cock  = { s->pos.x + cosf(h) * 0.03f,
                 s->pos.y + sinf(h) * 0.03f };
    float flicker = s->invuln > 0.f
        ? (0.5f + 0.5f * sinf(s->invuln * 30.f)) : 1.f;

    push_line(st, nose, rL, ship_c.x, ship_c.y, ship_c.z,
              0.95f * flicker, 2.6f);
    push_line(st, rL, cock, ship_c.x, ship_c.y, ship_c.z,
              0.95f * flicker, 2.6f);
    push_line(st, cock, rR, ship_c.x, ship_c.y, ship_c.z,
              0.95f * flicker, 2.6f);
    push_line(st, rR, nose, ship_c.x, ship_c.y, ship_c.z,
              0.95f * flicker, 2.6f);
    /* Hot inner spine — bright white core along the heading. */
    push_line(st, s->pos, nose, ship_bright.x, ship_bright.y, ship_bright.z,
              0.85f * flicker, 1.4f);
    push_line(st, cock, s->pos, ship_bright.x, ship_bright.y, ship_bright.z,
              0.65f * flicker, 1.2f);

    if (s->thrust > 0.05f) {
        V3 plume_c = hsl_to_rgb(st->plume_hue, 1.f, 0.6f);
        float L = 0.04f + s->plume_len * 0.10f;
        V2 back = { s->pos.x - cosf(h) * L, s->pos.y - sinf(h) * L };
        V2 bL   = { back.x + cosf(h + 2.7f) * L * 0.4f,
                    back.y + sinf(h + 2.7f) * L * 0.4f };
        V2 bR   = { back.x + cosf(h - 2.7f) * L * 0.4f,
                    back.y + sinf(h - 2.7f) * L * 0.4f };
        push_line(st, s->pos, bL, plume_c.x, plume_c.y, plume_c.z,
                  0.5f * s->thrust, 1.8f);
        push_line(st, s->pos, bR, plume_c.x, plume_c.y, plume_c.z,
                  0.5f * s->thrust, 1.8f);
        push_line(st, bL, bR, plume_c.x, plume_c.y, plume_c.z,
                  0.5f * s->thrust, 1.8f);
        V3 inner = hsl_to_rgb(st->plume_hue, 0.4f, 0.95f);
        V2 mid = { (bL.x + bR.x) * 0.5f, (bL.y + bR.y) * 0.5f };
        push_line(st, s->pos, mid, inner.x, inner.y, inner.z,
                  0.7f * s->thrust, 1.2f);
    }

    for (int i = 1; i < s->trail_n; i++) {
        float fade = 1.f - (float)i / (float)TRAIL_LEN;
        if (fade < 0.05f) continue;
        V3 tc = hsl_to_rgb(st->ship_hue, 0.7f, 0.55f);
        push_line(st, s->trail[i - 1], s->trail[i],
                  tc.x * 0.35f * fade, tc.y * 0.35f * fade, tc.z * 0.35f * fade,
                  0.18f * fade, 0.8f);
    }
}

static void push_rock_outline(State *st, int idx) {
    Rock *r = &st->rocks[idx];
    if (!r->alive) return;
    V3 col = colour_for_rock_tier((int)r->tier, r->hue_off);
    float vr = r->visual_radius;
    float base_a = r->rot;
    V2 prev = { r->pos.x + cosf(base_a) * vr * r->shape[0],
                r->pos.y + sinf(base_a) * vr * r->shape[0] };
    for (int i = 1; i <= r->vertices; i++) {
        int j = i % r->vertices;
        float a = base_a + (float)j / (float)r->vertices
                          * 2.f * (float)M_PI;
        V2 p = { r->pos.x + cosf(a) * vr * r->shape[j],
                 r->pos.y + sinf(a) * vr * r->shape[j] };
        float width = r->tier == ROCK_LARGE  ? 2.5f :
                      r->tier == ROCK_MEDIUM ? 2.0f : 1.5f;
        push_line(st, prev, p, col.x, col.y, col.z, 0.75f, width);
        prev = p;
    }

    if (r->tier != ROCK_SMALL) {
        V2 prev2 = { r->pos.x + cosf(r->rot * 1.3f) * vr * 0.55f,
                     r->pos.y + sinf(r->rot * 1.3f) * vr * 0.55f };
        for (int i = 1; i <= 6; i++) {
            int j = i % 6;
            float a = r->rot * 1.3f
                    + (float)j / 6.f * 2.f * (float)M_PI;
            V2 p = { r->pos.x + cosf(a) * vr * 0.55f,
                     r->pos.y + sinf(a) * vr * 0.55f };
            V3 col2 = hsl_to_rgb(fmodf(col.x + 0.5f, 1.f), 0.6f, 0.85f);
            push_line(st, prev2, p, col2.x, col2.y, col2.z, 0.35f, 1.0f);
            prev2 = p;
        }
    }

    for (int i = 1; i < r->trail_n; i++) {
        float fade = 1.f - (float)i / (float)TRAIL_LEN;
        if (fade < 0.1f) continue;
        push_line(st, r->trail[i - 1], r->trail[i],
                  col.x * 0.35f * fade, col.y * 0.35f * fade, col.z * 0.35f * fade,
                  0.12f * fade, 0.8f);
    }
}

static void push_bullet_outline(State *st, int idx) {
    Bullet *b = &st->bullets[idx];
    if (!b->alive) return;
    V3 c = colour_for_bullet(b->age);
    V2 tip  = { b->pos.x + b->vel.x * 0.04f,
                b->pos.y + b->vel.y * 0.04f };
    V2 tail = { b->pos.x - b->vel.x * 0.02f,
                b->pos.y - b->vel.y * 0.02f };
    push_line(st, tail, tip, c.x, c.y, c.z, 0.95f, 2.2f);
    for (int i = 1; i < b->trail_n; i++) {
        float fade = 1.f - (float)i / (float)TRAIL_LEN;
        if (fade < 0.05f) continue;
        push_line(st, b->trail[i - 1], b->trail[i],
                  c.x * 0.7f * fade, c.y * 0.7f * fade, c.z * 0.7f * fade,
                  0.30f * fade, 1.4f);
    }
}

static void push_debris_outline(State *st, int idx) {
    Debris *d = &st->debris[idx];
    if (!d->alive) return;
    float t = d->age / d->life;
    float fade = 1.f - t;
    if (fade <= 0.f) return;
    V3 c = hsl_to_rgb(d->hue, 0.9f, 0.6f);
    float L = 0.015f + t * 0.025f;
    V2 tip  = { d->pos.x + d->vel.x * L, d->pos.y + d->vel.y * L };
    V2 tail = { d->pos.x - d->vel.x * L, d->pos.y - d->vel.y * L };
    push_line(st, tail, tip, c.x, c.y, c.z, 0.7f * fade, 1.4f);
}

static void push_particles_outline(State *st) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &st->particles[i];
        if (!p->alive) continue;
        float t = p->age / p->life;
        float fade = 1.f - t;
        if (fade <= 0.f) continue;
        V3 c = hsl_to_rgb(p->hue, 1.f, 0.7f + 0.3f * (1.f - t));
        float a = (float)i * 0.21f + t * 3.f;
        V2 q = { p->pos.x + cosf(a) * 0.005f,
                 p->pos.y + sinf(a) * 0.005f };
        push_line(st, p->pos, q, c.x, c.y, c.z, 0.85f * fade, 1.6f);
    }
}

/* ===================================================================== */
/* Game step + render                                                      */
/* ===================================================================== */

static void game_step(State *st, float dt) {
    st->palette_phase = fmodf(st->palette_phase + st->palette_rate * dt, 1.f);
    ai_update(st, dt);
    fire_bullet(st);
    physics_step(st, dt);
    collision_step(st);
    waves_update(st, dt);
}

static void game_render(State *st) {
    st->line_n = 0;
    push_ship_outline(st);
    for (int i = 0; i < MAX_ROCKS; i++) push_rock_outline(st, i);
    for (int i = 0; i < MAX_BULLETS; i++) push_bullet_outline(st, i);
    for (int i = 0; i < MAX_DEBRIS; i++) push_debris_outline(st, i);
    push_particles_outline(st);

    /* ping-pong: decay previous frame into current write FBO,
     * then additively rasterise thick strokes into it (which now
     * compose on top of the decayed phosphor trail), then composite
     * the write-FBO to the visible default framebuffer. */
    draw_trail_decay(st);
    draw_lines(st);
    draw_composite(st);

    /* End-of-frame swap: the FBO we just wrote is the source for the
     * next frame's decay pass. */
    st->trail_idx = 1 - st->trail_idx;
}

/* ===================================================================== */
/* Init / draw / free                                                      */
/* ===================================================================== */

static void init_neonspacewar(ModeInfo *m) {
    State *st = calloc(1, sizeof *st);
    if (!st) { ncz_harness_die(1); return; }
    m->data = st;
    int mm = melee_init();
    if (mm < 0) { ncz_harness_die(1); return; }
    if (mm > 0) return;   /* Melee owns everything; the classic state stays empty */

    st->fb_w = 1920;
    st->fb_h = 1080;

    /* Identity MVP until reshape sets the aspect. */
    float ident[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f,
    };
    memcpy(st->mvp, ident, sizeof ident);

    /* ------------------------------------------------------------------ */
    /* Lines program: thick extruded-quad strokes with sharp core + halo  */
    /* ------------------------------------------------------------------ */
    static const char *line_vs =
        "#version 300 es\n"
        "layout(location=0) in vec2 a_pos;\n"
        "layout(location=1) in vec2 a_other;\n"
        "layout(location=2) in vec3 a_color;\n"
        "layout(location=3) in float a_alpha;\n"
        "layout(location=4) in float a_width;\n"
        "layout(location=5) in float a_side;\n"
        "uniform mat4 u_mvp;\n"
        "uniform float u_px_to_clip;\n"  /* pixels to NDC units */
        "out vec3 v_color;\n"
        "out float v_alpha;\n"
        "out float v_side;\n"
        "void main(){\n"
        "  vec4 p  = u_mvp * vec4(a_pos,    0.0, 1.0);\n"
        "  vec4 po = u_mvp * vec4(a_other, 0.0, 1.0);\n"
        "  vec2 d  = po.xy - p.xy;\n"
        "  float len2 = dot(d, d);\n"
        "  d = (len2 > 1e-10) ? d * inversesqrt(len2) : vec2(1.0, 0.0);\n"
        "  vec2 n  = vec2(-d.y, d.x);\n"
        "  vec2 off = n * (a_side * a_width * u_px_to_clip);\n"
        "  v_color = a_color;\n"
        "  v_alpha = a_alpha;\n"
        "  v_side  = a_side;\n"
        "  gl_Position = vec4(p.xy + off, 0.0, 1.0);\n"
        "}\n";

    static const char *line_fs =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec3 v_color;\n"
        "in float v_alpha;\n"
        "in float v_side;\n"
        "out vec4 o_col;\n"
        "void main(){\n"
        "  /* d in [0..1] across the half-quad (v_side is +-1 at the edges,\n"
        "   * so abs(v_side)=1 at the outer edge, -> 0 at the line center).\n"
        "   * exp(-d*d*k) gives a soft halo without a blur pass. */\n"
        "  float d = abs(v_side);\n"
        "  float core = 1.0 - smoothstep(0.55, 1.0, d);\n"
        "  float halo = exp(-d * d * 3.5);\n"
        "  vec3 c = v_color * v_alpha * (core * 1.6 + halo * 0.55);\n"
        "  o_col = vec4(c, core * v_alpha + halo * 0.25);\n"
        "}\n";

    /* ------------------------------------------------------------------ */
    /* Trail-fade program: per-channel phosphor decay on the FBO.         */
    /* Inputs are the previous-frame trail texture + a per-channel       */
    /* decay vector; outputs the decayed (and re-energised by line       */
    /* draws later this frame) buffer for the composite step.            */
    /* ------------------------------------------------------------------ */
    static const char *trail_vs =
        "#version 300 es\n"
        "layout(location=0) in vec2 a_pos;\n"
        "out vec2 v_uv;\n"
        "void main(){\n"
        "  v_uv = a_pos * 0.5 + 0.5;\n"
        "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "}\n";

    static const char *trail_fs =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec2 v_uv;\n"
        "uniform sampler2D u_prev;\n"
        "uniform vec3 u_decay;          /* per-channel decay, e.g. (0.95, 0.97, 0.995) */\n"
        "uniform float u_decay_strength;/* 0..1 -- 1.0 means full per-channel decay */\n"
        "out vec4 o_col;\n"
        "void main(){\n"
        "  vec3 p = texture(u_prev, v_uv).rgb;\n"
        "  vec3 d = mix(vec3(1.0), u_decay, u_decay_strength);\n"
        "  // Per-channel decay; blue lingers longer than red.\n"
        "  // Clamp low to avoid underflow noise on the trailing edge.\n"
        "  vec3 f = p * d;\n"
        "  float m = max(max(f.r, f.g), f.b);\n"
        "  if (m < 0.004) f = vec3(0.0);\n"
        "  o_col = vec4(f, 1.0);\n"
        "}\n";

    char *comp_src = load_shader_text("composite.frag");
    if (!comp_src) ncz_harness_die(1);

    char *lines_src = load_shader_text("lines.frag");
    st->line_prog = build_program("lines",
        line_vs, lines_src ? lines_src : line_fs);
    if (lines_src) free(lines_src);
    st->trail_prog = build_program("trail", trail_vs, trail_fs);
    st->comp_prog  = build_program("composite", trail_vs, comp_src);
    free(comp_src);

    if (!st->line_prog || !st->trail_prog || !st->comp_prog)
        ncz_harness_die(1);

    st->u_line_mvp = glGetUniformLocation(st->line_prog, "u_mvp");
    st->u_line_px_to_clip = glGetUniformLocation(st->line_prog, "u_px_to_clip");
    st->u_trail_prev = glGetUniformLocation(st->trail_prog, "u_prev");
    st->u_trail_decay = glGetUniformLocation(st->trail_prog, "u_decay");
    st->u_trail_decay_strength =
        glGetUniformLocation(st->trail_prog, "u_decay_strength");
    st->u_comp_samp = glGetUniformLocation(st->comp_prog, "u_trail");
    st->u_comp_resolution = glGetUniformLocation(st->comp_prog, "u_resolution");
    st->u_comp_time = glGetUniformLocation(st->comp_prog, "u_time");
    st->u_comp_palette_phase =
        glGetUniformLocation(st->comp_prog, "u_palette_phase");
    st->u_comp_palette_idx =
        glGetUniformLocation(st->comp_prog, "u_palette_idx");
    st->u_comp_chroma = glGetUniformLocation(st->comp_prog, "u_chroma");
    st->u_comp_bloom = glGetUniformLocation(st->comp_prog, "u_bloom");
    st->u_comp_wave = glGetUniformLocation(st->comp_prog, "u_wave");
    st->u_comp_shockwave_xy_strength_age =
        glGetUniformLocation(st->comp_prog, "u_shockwaves");
    st->u_comp_shockwave_count =
        glGetUniformLocation(st->comp_prog, "u_shockwave_count");

    setup_geometry_vbo(&st->full_vbo);
    setup_line_vbo();

    /* Two ping-pong trail FBOs. trail_idx identifies the *write* FBO
     * this frame; the decay pass reads the other one. */
    st->trail_fbo_w = st->fb_w;
    st->trail_fbo_h = st->fb_h;
    if (create_fbo(st->trail_fbo_w, st->trail_fbo_h,
                   &st->trail_tex[0], &st->trail_fbo[0]) < 0
        || create_fbo(st->trail_fbo_w, st->trail_fbo_h,
                      &st->trail_tex[1], &st->trail_fbo[1]) < 0)
        ncz_harness_die(1);
    st->trail_idx = 0;
    /* Both FBOs seeded with the same baseline phosphor colour, so the
     * very first frame (when we have no "previous" trail yet) still
     * looks alive. The palette recolours it through the composite pass. */
    for (int i = 0; i < 2; i++)
        clear_fbo(st->trail_fbo[i], 0.06f, 0.05f, 0.10f, 1.f);

    game_init(st);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
}

static void reshape_neonspacewar(ModeInfo *m, int w, int h) {
    State *st = m->data;
    if (!st) return;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w == st->fb_w && h == st->fb_h) return;
    st->fb_w = w;
    st->fb_h = h;

    if (st->trail_tex[0]) glDeleteTextures(1, &st->trail_tex[0]);
    if (st->trail_tex[1]) glDeleteTextures(1, &st->trail_tex[1]);
    if (st->trail_fbo[0]) glDeleteFramebuffers(1, &st->trail_fbo[0]);
    if (st->trail_fbo[1]) glDeleteFramebuffers(1, &st->trail_fbo[1]);
    st->trail_tex[0] = st->trail_tex[1] = 0;
    st->trail_fbo[0] = st->trail_fbo[1] = 0;
    st->trail_fbo_w = w; st->trail_fbo_h = h;
    if (create_fbo(w, h, &st->trail_tex[0], &st->trail_fbo[0]) < 0
        || create_fbo(w, h, &st->trail_tex[1], &st->trail_fbo[1]) < 0)
        ncz_harness_die(1);
    for (int i = 0; i < 2; i++)
        clear_fbo(st->trail_fbo[i], 0.06f, 0.05f, 0.10f, 1.f);

    /* Aspect-correct ortho: keep [-1,1] playfield, fit to longer axis. */
    float sx = 1.f, sy = 1.f;
    if (w > h) sx = (float)h / (float)w;
    else        sy = (float)w / (float)h;
    float mvp[16] = {
        sx, 0.f, 0.f, 0.f,
        0.f, sy, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f,
    };
    memcpy(st->mvp, mvp, sizeof mvp);
}

static void draw_neonspacewar(ModeInfo *m) {
    State *st = m->data;
    int w = m->xgwa.width, h = m->xgwa.height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (g_melee) { melee_draw(w, h); return; }
    if (!st || !st->line_prog) return;

    if (w != st->fb_w || h != st->fb_h) {
        reshape_neonspacewar(m, w, h);
    }

    double t_now = now_monotonic();
    static double t_prev = 0.0;
    if (t_prev == 0.0) t_prev = t_now;
    float dt = (float)(t_now - t_prev);
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.0f) dt = 0.0f;
    t_prev = t_now;
    game_step(st, dt);

    game_render(st);

    static unsigned long _ft_counter;
    static double _ft_prev;
    static int _ft_enabled;
    if (!_ft_enabled)
        _ft_enabled = (getenv("NCZ_NEO_SPACEWAR_PERF_LOG") != NULL);
    if (_ft_enabled) {
        if ((++_ft_counter % 30) == 0) {
            if (_ft_prev > 0.0) {
                double _dt = (t_now - _ft_prev) / 30.0;
                fprintf(stderr,
                    "[diag] neonspacewar frame_t frame=%lu dt_ms=%.3f\n",
                    _ft_counter, _dt * 1000.0);
                fflush(stderr);
            }
            _ft_prev = t_now;
        }
    }

    static unsigned long _gd_counter;
    if ((++_gd_counter % 180) == 0) {
        double elapsed = t_now - st->started;
        double dpm = (double)st->deaths / (elapsed / 60.0);
        double rpm = (double)st->rocks_killed / (elapsed / 60.0);
        fprintf(stderr,
            "[diag] neonspacewar t=%.1fs wave=%d alive=%d killed=%d "
            "fired=%d deaths=%d (%.2f/min) rocks=%.1f/min\n",
            elapsed, st->wave, count_alive_rocks(st), st->rocks_killed,
            st->bullets_fired, st->deaths, dpm, rpm);
        fflush(stderr);
    }

    static int once;
    if (!once++) {
        unsigned char px[16] = {0};
        glReadPixels(w/2, h/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glReadPixels(w/4, h/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+4);
        glReadPixels(3*w/4, h/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+8);
        glReadPixels(w/2, h/4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px+12);
        GLenum e = glGetError();
        fprintf(stderr,
            "[diag] neonspacewar first draw gl_error=0x%x samples="
            "ctr=%u,%u,%u; l=%u,%u,%u; r=%u,%u,%u; top=%u,%u,%u\n",
            e, px[0], px[1], px[2], px[4], px[5], px[6], px[8], px[9], px[10],
            px[12], px[13], px[14]);
        fflush(stderr);

        /* Also sample the trail FBO so we know whether the line
         * renderer actually got anything onto it. */
        glBindFramebuffer(GL_FRAMEBUFFER, st->trail_fbo[st->trail_idx]);
        unsigned char tpx[16] = {0};
        glReadPixels(st->trail_fbo_w/2, st->trail_fbo_h/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, tpx);
        glReadPixels(st->trail_fbo_w/4, st->trail_fbo_h/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, tpx+4);
        glReadPixels(3*st->trail_fbo_w/4, st->trail_fbo_h/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, tpx+8);
        glReadPixels(st->trail_fbo_w/2, st->trail_fbo_h/4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, tpx+12);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        fprintf(stderr,
            "[diag] neonspacewar trail FBO samples="
            "ctr=%u,%u,%u; l=%u,%u,%u; r=%u,%u,%u; top=%u,%u,%u\n",
            tpx[0], tpx[1], tpx[2], tpx[4], tpx[5], tpx[6], tpx[8], tpx[9], tpx[10],
            tpx[12], tpx[13], tpx[14]);
        fprintf(stderr, "[diag] neonspacewar line_n=%d fb=%dx%d trail_fbo=%u,%u tex=%u,%u idx=%d\n",
                st->line_n, st->fb_w, st->fb_h,
                st->trail_fbo[0], st->trail_fbo[1],
                st->trail_tex[0], st->trail_tex[1],
                st->trail_idx);
        fflush(stderr);
        once = 1;
    }
}

static void free_neonspacewar(ModeInfo *m) {
    State *st = m->data;
    melee_free();
    if (!st) return;
    if (st->line_prog) glDeleteProgram(st->line_prog);
    if (st->trail_prog) glDeleteProgram(st->trail_prog);
    if (st->comp_prog) glDeleteProgram(st->comp_prog);
    if (st->trail_tex[0]) glDeleteTextures(1, &st->trail_tex[0]);
    if (st->trail_tex[1]) glDeleteTextures(1, &st->trail_tex[1]);
    if (st->trail_fbo[0]) glDeleteFramebuffers(1, &st->trail_fbo[0]);
    if (st->trail_fbo[1]) glDeleteFramebuffers(1, &st->trail_fbo[1]);
    if (st->full_vbo) glDeleteBuffers(1, &st->full_vbo);
    if (g_line_vbo) { glDeleteBuffers(1, &g_line_vbo); g_line_vbo = 0; }
    free(st);
    m->data = NULL;
}

static Bool neonspacewar_handle_event(ModeInfo *m, XEvent *e) {
    (void)m; (void)e;
    return False;
}

static void release_neonspacewar(ModeInfo *m) { (void)m; }

static ModeSpecOpt neonspacewar_opts = { 0, NULL, 0, NULL, NULL };

struct xscreensaver_function_table neonspacewar_xscreensaver_function_table = {
    .name      = NCZ_NEO_SPACEWAR_NAME,
    .class_    = NCZ_NEO_SPACEWAR_CLASS,
    .init_cb   = init_neonspacewar,
    .draw_cb   = draw_neonspacewar,
    .reshape_cb = reshape_neonspacewar,
    .event_cb  = neonspacewar_handle_event,
    .free_cb   = free_neonspacewar,
    .release_cb = release_neonspacewar,
    .opts      = &neonspacewar_opts,
    .defaults_str = DEFAULTS,
};
