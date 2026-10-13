/* SPDX-License-Identifier: Apache-2.0 */
/* magmasimplex_goo_sim.h - the wax-body simulation behind engine=goo.
 * Pure C, no GL, so it can be tested headless (tests/test_magmasimplex_goo.c).
 */
#ifndef MAGMASIMPLEX_GOO_SIM_H
#define MAGMASIMPLEX_GOO_SIM_H
#include <stdint.h>

#define MS_GOO_MAXB 24
#define MS_GOO_MAXS 6
#define MS_GOO_POOL_Y (-0.80f)

typedef struct {
    float x, y, vx, vy;
    float r, rt;        /* radius and the radius it is relaxing toward */
    float T;            /* temperature 0..1 */
    float hue;          /* 0 = wax colour, 1 = secondary colour */
    float visc;         /* per-body damping per second */
    float wob;          /* wobble phase */
    int mode;           /* 0 free, 1 budding from the pool, 2 draining into a partner, 3 draining into the pool */
    int partner;        /* body this one drains into (mode 2), -1 for none */
    int alive;
    int tex;            /* texture id 0..63: pits and marbling travel with the body */
    unsigned id;        /* unique per body ever created: strands check it, so a
                         * reused slot is never mistaken for the old body */
} MsGooBody;

/* A strand of wax: a neck still joining a lifted body to the pool, a tower
 * stalk under a mushroom cap, or a stalactite hanging from the cap mass.
 * Rendered as a tapered, optionally beaded capsule from (x0,y0) to (x1,y1). */
typedef struct {
    float x0, y0, x1, y1;
    float r0, r1;       /* radius at each end */
    float bead;         /* 0 smooth .. 1 strongly beaded (nodular) */
    float T;
    float age, life;    /* the strand thins linearly and snaps at life */
    float len, len_t;   /* stalactites: current and target length */
    float base;         /* neck: radius scale at the start */
    float lobes;        /* tower: how knobbly (stacked, knuckled lobes) */
    int body;           /* neck/tower: slot of the body it holds, -1 for none */
    unsigned body_id;   /* ...and that body's id */
    int retracting;     /* stalactite: dropped its tip (or could not), shrinking */
    int kind;           /* 0 neck, 1 tower stalk, 2 stalactite */
    int alive;
} MsGooStrand;

typedef struct {
    MsGooBody b[MS_GOO_MAXB];
    MsGooStrand s[MS_GOO_MAXS];
    int maxn;
    int multicolour;
    int cap;            /* 1: cooled wax collects under the top (stalactites) */
    uint32_t rng;
    double t, next_bud, next_drip;
    float asp;
    unsigned long buds, merges, splits, drains, snaps, drips;
    int few;            /* fewer than three separate masses at the last check */
    double next_check;  /* when to check again */
    int next_tex;
    unsigned next_id;
    /* Tunables (1 = calibrated default): per-body damping, heating rate from
     * the bulb, and the wax held in the column. */
    float visc_k, heat_k, wax_k;
    /* Startup: 0 = a lamp just switched on (cold slab, no bodies) .. 1 = warm.
     * Rises over warm_s simulated seconds; 1 when startup is off. */
    float warm, warm_s;
} MsGooSim;

/* maxn: body budget (12 light, 20 full). Seeds bodies and runs the cycle
 * for prewarm_s simulated seconds so the first frame is mid-cycle. */
void ms_goo_sim_init(MsGooSim *s, uint32_t seed, int maxn, int multicolour, float asp, float prewarm_s);
/* Same, with the cap mass and stalactites enabled or not (init uses 0). */
void ms_goo_sim_init_cap(MsGooSim *s, uint32_t seed, int maxn, int multicolour, float asp, float prewarm_s, int cap);
/* Startup from cold: no seeded bodies, no prewarm; the pool warms over
 * warm_s simulated seconds before it buds. Call instead of the init above. */
void ms_goo_sim_init_cold(MsGooSim *s, uint32_t seed, int maxn, int multicolour, float asp, int cap, float warm_s);
void ms_goo_sim_step(MsGooSim *s, float dt);
/* Separate wax masses a viewer sees: bodies whose iso-surfaces touch are one
 * mass, and bodies still joined to the pool do not count. */
int ms_goo_sim_visible(const MsGooSim *s);
int ms_goo_sim_alive(const MsGooSim *s);
/* Total wax area held in the bodies (pi*r^2, the same measure the budget and
 * the merge/bud bookkeeping use). The lamp's wax is conserved: this stays
 * near its initial value while the pool takes what the column lets go.
 * Tests use it to bound drift over a run. */
float ms_goo_sim_wax_area(const MsGooSim *s);

#endif
