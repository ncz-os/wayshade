/* SPDX-License-Identifier: Apache-2.0 */
/* nsw_melee.h - neonspacewar "Melee": self-playing battles between original
 * alien races. The simulation (nsw_melee_sim.c) is pure C and deterministic
 * for a given seed, so it runs headless in tests/test_nsw_melee.c; the
 * renderer (nsw_melee_render.c) only reads it.
 *
 * World units: the arena is a disc of radius NSW_ARENA_R around a planet at
 * the origin. Time is seconds of simulation time.
 */
#ifndef NSW_MELEE_H
#define NSW_MELEE_H
#include <stdint.h>

#define NSW_RACES 7
#define NSW_MAX_SHIPS 4
#define NSW_MAX_SHOTS 256
#define NSW_MAX_PARTS 1024
#define NSW_MAX_DEBRIS 320
#define NSW_MAX_FX 32
#define NSW_TRAIL 32
#define NSW_ARENA_R 1.6f
#define NSW_PLANET_R 0.10f         /* default planet radius (NswConfig.planet scales it) */
#define NSW_BATTLE_MAX 90.f         /* default time limit (NswConfig.battle_max) */
#define NSW_DT (1.f / 120.f)        /* the fixed simulation substep */
#define NSW_ARENA_MIN 0.8f          /* the boundary never contracts below this */

typedef struct { float x, y; } NswV2;

/* One line segment of a hull, emblem or debris piece, in local units. */
typedef struct { float x0, y0, x1, y1; float bright; } NswSeg;

enum { W_SPREAD, W_PLASMA, W_HOMING, W_CHAIN, W_BOOMERANG, W_FLAK, W_LANCE };
enum { S_BLINK, S_SHIELD, S_MINES, S_DRONES, S_CLOAK, S_TRACTOR, S_REPULSE };
enum { T_KITE, T_CLOSE, T_ORBIT, T_FLEE, T_RAM, T_SNIPE };
enum { SH_BOLT, SH_PLASMA, SH_DART, SH_MINE, SH_DRONE, SH_BOOMERANG, SH_PELLET, SH_LANCE };
enum { FX_CHAIN, FX_TRACTOR, FX_REPULSE, FX_BLINK, FX_SHIELD_HIT, FX_EXPLOSION };
enum { PH_INTRO, PH_FIGHT, PH_OUTRO };

typedef struct {
    const char *name;           /* internal only, never drawn */
    float hue;                  /* 0..1 */
    float scale;                /* hull radius, world units */
    float mass, thrust, turn, drag, vmax;
    float hp, energy, regen;
    int weapon, special, tactic;
    float range;                /* preferred engagement range */
    float fire_cd, fire_cost, special_cd, special_cost;
    const NswSeg *hull;
    int nhull;
    const NswSeg *emblem;
    int nemblem;
    NswV2 engine[2];            /* exhaust points, local */
    int nengine;
    float ram_mult;             /* collision damage dealt, x (the rammer hits harder) */
} NswRace;

typedef struct {
    int alive, race, team;
    NswV2 p, v;
    float a;                    /* heading, radians */
    float hp, energy;
    float fire_t, special_t;    /* cooldown timers */
    float shield_t, cloak_t, tractor_t, hit_t, planet_t;
    int thrusting, tractor_target;
    NswV2 last_seen[NSW_MAX_SHIPS]; /* where this pilot last saw each ship */
    float mode_t;               /* AI: time left in the current manoeuvre */
    int mode;                   /* AI: 0 reposition, 1 attack run */
    float orbit_dir;
    NswV2 trail[NSW_TRAIL];
    int ntrail;
    float trail_t;
    float death_t;              /* time of destruction, for the renderer */
    uint64_t lost[2];           /* hull segments shot away (damage states) */
    int dmg_stage;              /* 0 intact; 1..3 below 30/20/10% hull */
    int shield_cracks;          /* hits taken by the current shield */
} NswShip;

typedef struct {
    int alive, type, owner, team, target;
    NswV2 p, v, prev[6];
    int nprev;
    float life, age, dmg;
} NswShot;

typedef struct { int alive; NswV2 p, v; float life, max_life, hue, len, bright; } NswPart;

typedef struct {
    int alive;
    NswV2 p, v;
    float a, w, life, max_life, hue;
    NswSeg seg;
} NswDebris;

typedef struct { int alive, type, a, b; NswV2 p0, p1; float t, dur, hue; } NswFx;

enum { NSW_MATCH_MIX, NSW_MATCH_DUEL, NSW_MATCH_TEAMS, NSW_MATCH_FFA };

/* What a run may vary (the hack's options; see nsw_config_default). */
typedef struct {
    uint32_t race_mask;         /* races that may fight, one bit each; fewer than two = all */
    int matchups;               /* NSW_MATCH_*: 1 v 1, 2 v 2 and three-way, or only one of them */
    float gravity;              /* planet mass, x the default (0 = no pull) */
    float planet;               /* planet radius, x the default */
    float contract;             /* how fast the boundary closes in a stalemate, x the default; 0 = never */
    float battle_max;           /* time limit, seconds */
} NswConfig;

typedef struct {
    NswConfig cfg;
    float planet_r, gm;         /* from cfg */
    uint32_t rng;               /* every draw that can change the battle */
    uint32_t fxrng;             /* cosmetic draws only (particles, debris) */
    double t;                   /* simulation seconds since the run began */
    float battle_t, phase_t, slowmo_t;
    float freeze_t;             /* wall seconds of hit-stop left after a kill */
    float arena_r;              /* current boundary radius (contracts in a stalemate) */
    float calm_t;               /* battle seconds since anyone was hit */
    float aggro;                /* 0..1: pilots press in while the boundary closes */
    int kills;                  /* ships destroyed since the run began */
    NswV2 kill_p;               /* where the latest one died */
    unsigned long hits;         /* hits landed since the run began (camera shake) */
    int phase, battle, nships;
    int matchup[NSW_MAX_SHIPS];
    int prev_races[NSW_MAX_SHIPS], nprev_races;
    int winner_team;
    NswShip ship[NSW_MAX_SHIPS];
    NswShot shot[NSW_MAX_SHOTS];
    NswPart part[NSW_MAX_PARTS];
    NswDebris debris[NSW_MAX_DEBRIS];
    NswFx fx[NSW_MAX_FX];
    int part_budget;            /* particles per event scale (weak GPUs get fewer) */
    float acc;                  /* time not yet simulated (fixed substeps) */
    /* statistics for the log and the tests */
    unsigned long battles_done, timeouts;
    float last_duration;
    int last_winner_races[NSW_MAX_SHIPS], last_nwinners;
    int nan_seen;
    float dmg_by[4];            /* hull damage (after shields) by shots, rams, planet, specials */
    uint32_t last_hash;         /* nsw_state_hash() when the last battle was decided */
    unsigned long contractions; /* battles in which the boundary had to close */
    int closing;                /* this battle's boundary has started to close */
    /* This battle's measurements, for the balance tests (reset at each start). */
    struct {
        int specials[NSW_MAX_SHIPS];      /* special abilities used, per ship */
        float deficit[NSW_MAX_SHIPS];     /* worst gap between this ship's hull share and the best enemy's */
        float dist_sum, dist_sq;          /* distance to the nearest enemy, sampled every 0.5 s of fight */
        int dist_n;
        float longest_calm;               /* longest stretch without a hit, seconds */
        int planet_kills;                 /* ships the planet finished off */
        float sample_t;
    } bs;
} NswWorld;

/* The race table; hulls and emblems are built on first use (nsw_init). */
extern NswRace NSW_RACE[NSW_RACES];
void nsw_build_races(void);

void nsw_config_default(NswConfig *c);
/* weak: fewer particles. nsw_init uses the default configuration. */
void nsw_init(NswWorld *w, uint32_t seed, int weak);
void nsw_init_cfg(NswWorld *w, uint32_t seed, int weak, const NswConfig *cfg);
/* Advance by dt seconds of wall time (hit-stop and slow motion are applied
 * inside). A slice over 0.1 s is clamped to 0.1 s: after a stall the battle
 * resumes rather than jumping ahead. The clamp drops time, never changes
 * the outcome, since battles advance only in whole NSW_DT substeps. */
void nsw_step(NswWorld *w, float dt);
/* Start a specific matchup (tests); races[i], teams[i] for n ships. */
void nsw_start_battle(NswWorld *w, const int *races, const int *teams, int n);
/* Time scale currently applied: 0 during the hit-stop after a kill, 0.25 in
 * the slow motion that follows, else 1. */
float nsw_time_scale(const NswWorld *w);
/* Hash of the battle state: the gameplay random stream, the arena, every
 * ship's motion, hull, energy, cooldowns and pilot state, and every live
 * shot's motion, lifetime, damage and targeting. Cosmetic state (particles,
 * wreckage, which lattice pieces broke off, shot streak history) is left
 * out. Two runs with the same seed give the same hashes however
 * time was sliced and whatever the quality level. */
uint32_t nsw_state_hash(const NswWorld *w);
/* Simulation time not yet stepped, 0..NSW_DT; the renderer draws moving
 * things this far behind their stepped position so motion stays smooth. */
float nsw_lag(const NswWorld *w);

#endif
