/* SPDX-License-Identifier: Apache-2.0 */
/* nsw_melee_sim.c - the Melee simulation: seven original races, their
 * pilots, weapons and specials, a planet with gravity at the centre of a
 * bounded arena, and battles that resolve and give way to the next.
 *
 * Deterministic for a seed: every draw that can change a battle comes from
 * w->rng and cosmetic ones (particles, wreckage) from w->fxrng, so the
 * quality level never changes the outcome; the step is a fixed 1/120 s
 * substep however the caller slices time (tests/test_nsw_melee.c replays
 * 200 seeds both ways and compares state hashes). The renderer only reads
 * this state.
 *
 * The races are original. Names below are internal identifiers from an
 * invented phonology; nothing here is drawn as text.
 */
#include "nsw_melee.h"
#include <math.h>
#include <string.h>

#define DT NSW_DT
#define PI 3.14159265f
#define GM 0.028f           /* default planet mass (NswConfig.gravity scales it) */

/* ---------------------------------------------------------------- utils */
static float urand(uint32_t *s) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return (float)(*s & 0xffffffu) / 16777216.f;
}
static float frand(NswWorld *w, float a, float b) { return a + (b - a) * urand(&w->rng); }
/* Cosmetic randomness: never feeds back into the battle. */
static float crand(NswWorld *w, float a, float b) { return a + (b - a) * urand(&w->fxrng); }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static NswV2 v2(float x, float y) { NswV2 r = {x, y}; return r; }
static NswV2 add(NswV2 a, NswV2 b) { return v2(a.x + b.x, a.y + b.y); }
static NswV2 sub(NswV2 a, NswV2 b) { return v2(a.x - b.x, a.y - b.y); }
static NswV2 mul(NswV2 a, float k) { return v2(a.x * k, a.y * k); }
static float dot(NswV2 a, NswV2 b) { return a.x * b.x + a.y * b.y; }
static float len(NswV2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
static NswV2 norm(NswV2 a) { float l = len(a); return l > 1e-6f ? mul(a, 1.f / l) : v2(1, 0); }
static NswV2 dir(float a) { return v2(cosf(a), sinf(a)); }
static float wrap_angle(float a) {
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

/* ---------------------------------------------------------------- hulls */
static NswSeg hulls[NSW_RACES][96], emblems[NSW_RACES][48];
/* NswShip.lost is a 128-bit mask over hull segments. */
_Static_assert(sizeof hulls[0] / sizeof hulls[0][0] <= 64 * sizeof(((NswShip *)0)->lost) / sizeof(uint64_t),
               "hull segment cap exceeds NswShip.lost");
static int nh[NSW_RACES], ne[NSW_RACES];

static void seg(NswSeg *s, int *n, int cap, float x0, float y0, float x1, float y1, float b) {
    if (*n >= cap) return;
    NswSeg q = {x0, y0, x1, y1, b};
    s[(*n)++] = q;
}
/* Closed polygon from points, optionally mirrored across the x axis. */
static void poly(NswSeg *s, int *n, int cap, const float *pts, int np, float b) {
    for (int i = 0; i < np; i++) {
        int j = (i + 1) % np;
        seg(s, n, cap, pts[2 * i], pts[2 * i + 1], pts[2 * j], pts[2 * j + 1], b);
    }
}
static void ngon(NswSeg *s, int *n, int cap, float cx, float cy, float r, int k, float rot, float b) {
    for (int i = 0; i < k; i++) {
        float a0 = rot + 2 * PI * i / k, a1 = rot + 2 * PI * (i + 1) / k;
        seg(s, n, cap, cx + r * cosf(a0), cy + r * sinf(a0), cx + r * cosf(a1), cy + r * sinf(a1), b);
    }
}
static void arc(NswSeg *s, int *n, int cap, float cx, float cy, float r, float a0, float a1, int k, float b) {
    for (int i = 0; i < k; i++) {
        float t0 = a0 + (a1 - a0) * i / k, t1 = a0 + (a1 - a0) * (i + 1) / k;
        seg(s, n, cap, cx + r * cosf(t0), cy + r * sinf(t0), cx + r * cosf(t1), cy + r * sinf(t1), b);
    }
}

static void build_hulls(void) {
    NswSeg *s;
    int *n;
    const int C = 96;
    /* 0: a needle with swept blades; agile, fragile */
    s = hulls[0]; n = &nh[0];
    {
        static const float o[] = {1.0f, 0, 0.25f, 0.16f, -0.25f, 0.62f, -0.55f, 0.58f, -0.32f, 0.14f, -0.72f, 0.1f,
                                  -0.72f, -0.1f, -0.32f, -0.14f, -0.55f, -0.58f, -0.25f, -0.62f, 0.25f, -0.16f};
        poly(s, n, C, o, 11, 1.f);
        seg(s, n, C, 0.85f, 0, -0.6f, 0, 0.7f);
        for (int k = -1; k <= 1; k += 2) {
            seg(s, n, C, 0.1f, 0.12f * k, -0.38f, 0.5f * k, 0.55f);
            seg(s, n, C, -0.05f, 0.14f * k, -0.5f, 0.3f * k, 0.45f);
            seg(s, n, C, 0.55f, 0.07f * k, 0.2f, 0.07f * k, 0.5f);
        }
    }
    /* 1: a hexagonal block of armour; slow and heavy */
    s = hulls[1]; n = &nh[1];
    ngon(s, n, C, 0, 0, 1.0f, 6, 0, 1.f);
    ngon(s, n, C, 0, 0, 0.55f, 6, PI / 6, 0.7f);
    for (int i = 0; i < 6; i++) {
        float a = 2 * PI * i / 6;
        seg(s, n, C, 0.55f * cosf(a + PI / 6), 0.55f * sinf(a + PI / 6), cosf(a), sinf(a), 0.5f);
        seg(s, n, C, 0.78f * cosf(a), 0.78f * sinf(a), 0.78f * cosf(a + PI / 3), 0.78f * sinf(a + PI / 3), 0.35f);
    }
    seg(s, n, C, 1.0f, 0.2f, 1.18f, 0.28f, 0.9f);
    seg(s, n, C, 1.0f, -0.2f, 1.18f, -0.28f, 0.9f);
    ngon(s, n, C, 0, 0, 0.2f, 6, 0, 0.8f);
    /* 2: a ring with three fins; drifts */
    s = hulls[2]; n = &nh[2];
    ngon(s, n, C, 0, 0, 0.72f, 14, 0, 1.f);
    ngon(s, n, C, 0, 0, 0.42f, 3, 0, 0.6f);
    for (int i = 0; i < 3; i++) {
        float a = 2 * PI * i / 3 + PI / 3;
        NswV2 d = dir(a), t = v2(-d.y, d.x);
        seg(s, n, C, 0.72f * d.x + 0.12f * t.x, 0.72f * d.y + 0.12f * t.y, 1.05f * d.x, 1.05f * d.y, 0.9f);
        seg(s, n, C, 0.72f * d.x - 0.12f * t.x, 0.72f * d.y - 0.12f * t.y, 1.05f * d.x, 1.05f * d.y, 0.9f);
        seg(s, n, C, 0.2f * d.x, 0.2f * d.y, 0.72f * d.x, 0.72f * d.y, 0.35f);
    }
    arc(s, n, C, 0, 0, 0.9f, -0.35f, 0.35f, 4, 0.8f);
    /* 3: a segmented insect body with legs */
    s = hulls[3]; n = &nh[3];
    {
        const float cx[3] = {0.62f, 0.05f, -0.6f}, rr[3] = {0.28f, 0.33f, 0.42f};
        for (int i = 0; i < 3; i++) ngon(s, n, C, cx[i], 0, rr[i], 4, 0, 1.f);
        for (int k = -1; k <= 1; k += 2) {
            for (int j = 0; j < 3; j++) {
                float x = 0.3f - 0.3f * j;
                seg(s, n, C, x, 0.15f * k, x - 0.15f, 0.62f * k, 0.6f);
                seg(s, n, C, x - 0.15f, 0.62f * k, x - 0.38f, 0.8f * k, 0.45f);
            }
            seg(s, n, C, 0.85f, 0.08f * k, 1.1f, 0.3f * k, 0.7f);
        }
        seg(s, n, C, -1.02f, 0, 0.9f, 0, 0.3f);
    }
    /* 4: a crescent that fights while fleeing (it fires astern) */
    s = hulls[4]; n = &nh[4];
    arc(s, n, C, 0, 0, 1.0f, -2.3f, 2.3f, 16, 1.f);
    arc(s, n, C, 0.38f, 0, 0.62f, -2.1f, 2.1f, 12, 0.8f);
    seg(s, n, C, cosf(2.3f), sinf(2.3f), 0.38f + 0.62f * cosf(2.1f), 0.62f * sinf(2.1f), 1.f);
    seg(s, n, C, cosf(-2.3f), sinf(-2.3f), 0.38f + 0.62f * cosf(-2.1f), 0.62f * sinf(-2.1f), 1.f);
    for (int i = 0; i < 5; i++) {
        float a = -1.6f + 0.8f * i;
        seg(s, n, C, 0.62f * cosf(a) + 0.38f * 0.3f, 0.62f * sinf(a) * 0.9f, cosf(a), sinf(a), 0.4f);
    }
    /* 5: a scrap wedge with a toothed ram */
    s = hulls[5]; n = &nh[5];
    {
        static const float o[] = {0.9f, 0.55f, -0.85f, 0.85f, -0.6f, 0, -0.85f, -0.85f, 0.9f, -0.55f};
        poly(s, n, C, o, 5, 1.f);
        for (int i = 0; i < 4; i++) {
            float y0 = 0.55f - 0.275f * i, y1 = y0 - 0.275f;
            seg(s, n, C, 0.9f, y0, 1.12f, 0.5f * (y0 + y1), 1.f);
            seg(s, n, C, 1.12f, 0.5f * (y0 + y1), 0.9f, y1, 1.f);
        }
        seg(s, n, C, -0.85f, 0.85f, 0.9f, -0.55f, 0.35f);
        seg(s, n, C, -0.85f, -0.85f, 0.9f, 0.55f, 0.35f);
        seg(s, n, C, 0.2f, 0.66f, 0.2f, -0.66f, 0.45f);
    }
    /* 6: a long lance with a ring; a sniper */
    s = hulls[6]; n = &nh[6];
    seg(s, n, C, 1.25f, 0, -0.85f, 0.08f, 1.f);
    seg(s, n, C, 1.25f, 0, -0.85f, -0.08f, 1.f);
    seg(s, n, C, -0.85f, 0.08f, -0.85f, -0.08f, 0.8f);
    ngon(s, n, C, -0.1f, 0, 0.48f, 8, PI / 8, 0.9f);
    ngon(s, n, C, -0.1f, 0, 0.3f, 8, 0, 0.45f);
    for (int k = -1; k <= 1; k += 2) {
        seg(s, n, C, -0.6f, 0.06f * k, -1.0f, 0.42f * k, 0.8f);
        seg(s, n, C, -1.0f, 0.42f * k, -0.85f, 0.08f * k, 0.6f);
        seg(s, n, C, 0.4f, 0.04f * k, 0.95f, 0.02f * k, 0.4f);
    }
}

/* Emblems: geometric marks for the title card. Not letters, not numerals. */
static void build_emblems(void) {
    const int C = 48;
    NswSeg *s;
    int *n;
    s = emblems[0]; n = &ne[0];
    ngon(s, n, C, 0, 0, 1.f, 10, 0, 1.f); ngon(s, n, C, 0, 0, 0.62f, 3, PI / 2, 1.f);
    s = emblems[1]; n = &ne[1];
    ngon(s, n, C, 0, 0, 1.f, 6, 0, 1.f); ngon(s, n, C, 0, 0, 0.55f, 6, PI / 6, 1.f); ngon(s, n, C, 0, 0, 0.18f, 6, 0, 1.f);
    s = emblems[2]; n = &ne[2];
    ngon(s, n, C, 0, 0, 1.f, 8, PI / 8, 1.f); ngon(s, n, C, 0.25f, 0.1f, 0.5f, 8, 0, 1.f);
    s = emblems[3]; n = &ne[3];
    for (int i = -1; i <= 1; i++) ngon(s, n, C, 0.62f * i, 0.62f * i, 0.4f, 4, 0, 1.f);
    s = emblems[4]; n = &ne[4];
    arc(s, n, C, 0, 0, 1.f, -2.4f, 2.4f, 14, 1.f); arc(s, n, C, 0.4f, 0, 0.66f, -2.2f, 2.2f, 10, 1.f);
    s = emblems[5]; n = &ne[5];
    for (int i = 0; i < 3; i++) {
        float a = PI / 2 + 2 * PI * i / 3;
        ngon(s, n, C, 0.55f * cosf(a), 0.55f * sinf(a), 0.36f, 4, PI / 4 + a, 1.f);
    }
    s = emblems[6]; n = &ne[6];
    for (int i = 0; i < 8; i++) {
        float a0 = 2 * PI * i / 8, a1 = 2 * PI * (i + 3) / 8;
        seg(s, n, C, cosf(a0), sinf(a0), cosf(a1), sinf(a1), 1.f);
    }
}

NswRace NSW_RACE[NSW_RACES] = {
    /* name       hue   scale  mass thr  turn drag vmax  hp  en  rgn  weapon       special    tactic  range  fcd  fcost scd  scost; hull, emblem, engines filled by nsw_build_races; ram damage x */
    {"vyrrh",    0.52f, 0.062f, 0.8f, 1.3f, 4.6f, 1.3f, 0.95f, 56, 20, 6, W_SPREAD,    S_BLINK,   T_KITE,  0.48f, 0.45f, 2, 6, 10, NULL, 0, NULL, 0, {{0, 0}, {0, 0}}, 0, 1.f},
    {"ossurak",  0.07f, 0.131f, 3.0f, 0.5f, 1.4f, 0.7f, 0.46f, 174, 30, 5, W_PLASMA,    S_SHIELD,  T_CLOSE, 0.32f, 1.3f, 6, 12, 15, NULL, 0, NULL, 0, {{0, 0}, {0, 0}}, 0, 1.f},
    {"thessil",  0.34f, 0.080f, 1.0f, 0.8f, 2.4f, 0.0f, 0.80f, 79, 24, 5, W_HOMING,    S_MINES,   T_ORBIT, 0.52f, 0.7f, 3, 8, 12, NULL, 0, NULL, 0, {{0, 0}, {0, 0}}, 0, 1.f},
    {"quorra",   0.15f, 0.098f, 1.6f, 0.75f, 2.3f, 0.8f, 0.62f, 102, 30, 8, W_CHAIN,    S_DRONES,  T_CLOSE, 0.34f, 0.9f, 5, 14, 16, NULL, 0, NULL, 0, {{0, 0}, {0, 0}}, 0, 1.f},
    {"mirel",    0.86f, 0.074f, 0.9f, 1.0f, 3.4f, 1.0f, 0.86f, 72, 22, 6, W_BOOMERANG, S_CLOAK,   T_FLEE,  0.55f, 0.62f, 3, 10, 12, NULL, 0, NULL, 0, {{0, 0}, {0, 0}}, 0, 1.f},
    {"graven",   0.00f, 0.105f, 2.2f, 0.95f, 2.1f, 0.5f, 0.78f, 118, 26, 6, W_FLAK,     S_TRACTOR, T_RAM,   0.12f, 0.6f, 3, 9, 12, NULL, 0, NULL, 0, {{0, 0}, {0, 0}}, 0, 2.f},
    {"aeth",     0.74f, 0.088f, 1.2f, 0.65f, 2.6f, 0.9f, 0.66f, 70, 34, 7, W_LANCE,    S_REPULSE, T_SNIPE, 0.85f, 1.6f, 7, 10, 14, NULL, 0, NULL, 0, {{0, 0}, {0, 0}}, 0, 1.f},
};

void nsw_build_races(void) {
    static int built;
    if (built) return;
    built = 1;
    build_hulls();
    build_emblems();
    for (int r = 0; r < NSW_RACES; r++) {
        NSW_RACE[r].hull = hulls[r];
        NSW_RACE[r].nhull = nh[r];
        NSW_RACE[r].emblem = emblems[r];
        NSW_RACE[r].nemblem = ne[r];
        NSW_RACE[r].nengine = 2;
        NSW_RACE[r].engine[0] = v2(-0.75f, 0.2f);
        NSW_RACE[r].engine[1] = v2(-0.75f, -0.2f);
    }
    /* The crescent's open side is its stern; it fires astern (W_BOOMERANG)
     * but thrusts forward like every other hull. */
    NSW_RACE[4].engine[0] = v2(-0.6f, 0.62f);
    NSW_RACE[4].engine[1] = v2(-0.6f, -0.62f);
}

/* ---------------------------------------------------------------- pools */
static void part(NswWorld *w, NswV2 p, NswV2 v, float life, float hue, float lenv, float bright) {
    for (int i = 0; i < NSW_MAX_PARTS; i++) {
        NswPart *q = &w->part[i];
        if (q->alive) continue;
        q->alive = 1; q->p = p; q->v = v; q->life = q->max_life = life;
        q->hue = hue; q->len = lenv; q->bright = bright;
        return;
    }
}
static void burst(NswWorld *w, NswV2 p, NswV2 base, int n, float speed, float hue, float life) {
    n = n * w->part_budget / 4;
    for (int i = 0; i < n; i++) {
        float a = crand(w, 0, 2 * PI), s = speed * crand(w, 0.2f, 1.f);
        part(w, p, add(base, mul(dir(a), s)), life * crand(w, 0.5f, 1.f), hue + crand(w, -0.04f, 0.04f), 0.02f, 1.f);
    }
}
static int shot_new(NswWorld *w) {
    for (int i = 0; i < NSW_MAX_SHOTS; i++)
        if (!w->shot[i].alive) { memset(&w->shot[i], 0, sizeof w->shot[i]); w->shot[i].alive = 1; w->shot[i].target = -1; return i; }
    return -1;
}
static void fx(NswWorld *w, int type, int a, int b, NswV2 p0, NswV2 p1, float dur, float hue) {
    for (int i = 0; i < NSW_MAX_FX; i++) {
        NswFx *f = &w->fx[i];
        if (f->alive) continue;
        f->alive = 1; f->type = type; f->a = a; f->b = b; f->p0 = p0; f->p1 = p1; f->t = 0; f->dur = dur; f->hue = hue;
        return;
    }
}

/* ---------------------------------------------------------------- battles */
static int teams_alive(const NswWorld *w, int *last_team) {
    int seen[NSW_MAX_SHIPS] = {0}, n = 0;
    for (int i = 0; i < w->nships; i++)
        if (w->ship[i].alive && !seen[w->ship[i].team]) { seen[w->ship[i].team] = 1; n++; *last_team = w->ship[i].team; }
    return n;
}

void nsw_start_battle(NswWorld *w, const int *races, const int *teams, int n) {
    memset(w->ship, 0, sizeof w->ship);
    memset(w->shot, 0, sizeof w->shot);
    memset(w->fx, 0, sizeof w->fx);
    w->nships = n;
    /* Three or four ships start in a tighter arena, so the camera can keep
     * every hull large and in frame. */
    w->arena_r = n > 2 ? NSW_ARENA_R * 0.78f : NSW_ARENA_R;
    float a0 = frand(w, 0, 2 * PI);
    for (int i = 0; i < n; i++) {
        NswShip *s = &w->ship[i];
        const NswRace *r = &NSW_RACE[races[i]];
        s->alive = 1; s->race = races[i]; s->team = teams[i];
        float a = a0 + 2 * PI * i / n;
        s->p = mul(dir(a), w->arena_r * 0.72f);
        s->a = a + PI + frand(w, -0.3f, 0.3f);
        s->v = mul(dir(s->a), 0.25f);
        s->hp = r->hp; s->energy = r->energy;
        s->fire_t = frand(w, 0.5f, 1.5f); s->special_t = frand(w, 2.f, 5.f);
        s->orbit_dir = urand(&w->rng) < 0.5f ? -1.f : 1.f;
        s->tractor_target = -1;
        for (int j = 0; j < NSW_MAX_SHIPS; j++) s->last_seen[j] = v2(0, 0);
        w->matchup[i] = races[i];
    }
    w->phase = PH_INTRO;
    w->phase_t = 0;
    w->battle_t = 0;
    w->winner_team = -1;
    w->calm_t = 0;
    w->aggro = 0;
    w->closing = 0;
    memset(&w->bs, 0, sizeof w->bs);
    w->battle++;
}

/* The same set of races, whatever the order, sides or ship count, counts as
 * a repeat (a 2 v 2 of the same two races looks like the duel before it). */
static int race_mask(const int *r, int n) {
    int m = 0;
    for (int i = 0; i < n; i++) m |= 1 << r[i];
    return m;
}
static int same_races(const int *a, int na, const int *b, int nb) {
    return na > 0 && race_mask(a, na) == race_mask(b, nb);
}

/* A new matchup from the allowed races: 1 v 1 most of the time, sometimes
 * 2 v 2 or three-way (or only the kind the configuration asks for); never
 * the same set of races twice in a row while there is another to pick. */
static void next_battle(NswWorld *w) {
    int races[NSW_MAX_SHIPS], teams[NSW_MAX_SHIPS], n;
    int allowed[NSW_RACES], na = 0;
    for (int r = 0; r < NSW_RACES; r++)
        if (w->cfg.race_mask >> r & 1u) allowed[na++] = r;
    float u = urand(&w->rng);
    int kind = w->cfg.matchups == NSW_MATCH_DUEL ? 0 : w->cfg.matchups == NSW_MATCH_TEAMS ? 1 :
               w->cfg.matchups == NSW_MATCH_FFA ? 2 : u < 0.68f ? 0 : u < 0.86f ? 1 : 2;
    if (kind == 2 && na < 3) kind = 0;
    n = kind == 0 ? 2 : kind == 1 ? 4 : 3;
    int pick = kind == 1 ? 2 : n;   /* a 2 v 2 is two races, a pair of each */
    for (int tries = 0; tries < 50; tries++) {
        int used = 0;
        for (int i = 0; i < pick; i++) {
            int r;
            do { r = allowed[(int)(urand(&w->rng) * na)]; } while (used & (1 << r));
            used |= 1 << r;
            races[i] = r;
        }
        if (kind == 1) { races[2] = races[0]; races[3] = races[1]; }
        if (!same_races(w->prev_races, w->nprev_races, races, n)) break;
    }
    for (int i = 0; i < n; i++) teams[i] = kind == 1 ? i % 2 : i;
    memcpy(w->prev_races, races, sizeof(int) * (size_t)n);
    w->nprev_races = n;
    nsw_start_battle(w, races, teams, n);
}

void nsw_config_default(NswConfig *c) {
    c->race_mask = (1u << NSW_RACES) - 1u;
    c->matchups = NSW_MATCH_MIX;
    c->gravity = 1.f;
    c->planet = 1.f;
    c->contract = 1.f;
    c->battle_max = NSW_BATTLE_MAX;
}

void nsw_init(NswWorld *w, uint32_t seed, int weak) { nsw_init_cfg(w, seed, weak, NULL); }

void nsw_init_cfg(NswWorld *w, uint32_t seed, int weak, const NswConfig *cfg) {
    nsw_build_races();
    memset(w, 0, sizeof *w);
    if (cfg) w->cfg = *cfg;
    else nsw_config_default(&w->cfg);
    NswConfig *c = &w->cfg;
    c->race_mask &= (1u << NSW_RACES) - 1u;
    int nr = 0;
    for (int r = 0; r < NSW_RACES; r++) nr += c->race_mask >> r & 1u;
    if (nr < 2) c->race_mask = (1u << NSW_RACES) - 1u;
    if (c->matchups < NSW_MATCH_MIX || c->matchups > NSW_MATCH_FFA) c->matchups = NSW_MATCH_MIX;
    c->gravity = isfinite(c->gravity) ? fminf(fmaxf(c->gravity, 0.f), 3.f) : 1.f;
    c->planet = isfinite(c->planet) ? fminf(fmaxf(c->planet, 0.5f), 2.f) : 1.f;
    c->contract = isfinite(c->contract) ? fminf(fmaxf(c->contract, 0.f), 3.f) : 1.f;
    c->battle_max = isfinite(c->battle_max) ? fminf(fmaxf(c->battle_max, 30.f), 180.f) : NSW_BATTLE_MAX;
    w->planet_r = NSW_PLANET_R * c->planet;
    w->gm = GM * c->gravity;
    w->rng = seed ? seed : 0x2545F491u;
    w->fxrng = w->rng ^ 0x9E3779B9u;
    if (!w->fxrng) w->fxrng = 1;
    w->part_budget = weak ? 2 : 4;
    next_battle(w);
}

float nsw_time_scale(const NswWorld *w) { return w->freeze_t > 0 ? 0.f : w->slowmo_t > 0 ? 0.25f : 1.f; }
float nsw_lag(const NswWorld *w) { return DT - w->acc; }

static uint32_t fnv(uint32_t h, const void *p, size_t n) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}
uint32_t nsw_state_hash(const NswWorld *w) {
    uint32_t h = 2166136261u;
    h = fnv(h, &w->rng, sizeof w->rng);
    h = fnv(h, &w->battle, sizeof w->battle);
    h = fnv(h, &w->battle_t, sizeof w->battle_t);
    h = fnv(h, &w->arena_r, sizeof w->arena_r);
    h = fnv(h, &w->aggro, sizeof w->aggro);
    h = fnv(h, &w->winner_team, sizeof w->winner_team);
    for (int i = 0; i < w->nships; i++) {
        const NswShip *s = &w->ship[i];
        const float f[] = {s->p.x, s->p.y, s->v.x, s->v.y, s->a, s->hp, s->energy, s->fire_t, s->special_t,
                           s->shield_t, s->cloak_t, s->tractor_t, s->planet_t, s->mode_t, s->orbit_dir};
        const int n[] = {s->alive, s->race, s->team, s->mode, s->tractor_target, s->dmg_stage};
        h = fnv(h, f, sizeof f);
        h = fnv(h, n, sizeof n);
    }
    for (int k = 0; k < NSW_MAX_SHOTS; k++) {
        const NswShot *q = &w->shot[k];
        if (!q->alive) continue;
        const float f[] = {q->p.x, q->p.y, q->v.x, q->v.y, q->life, q->age, q->dmg};
        const int n[] = {k, q->type, q->owner, q->team, q->target};
        h = fnv(h, f, sizeof f);
        h = fnv(h, n, sizeof n);
    }
    return h;
}

/* ---------------------------------------------------------------- damage */
/* One hull segment of ship i, flying free as wreckage. */
static void shed(NswWorld *w, int i, int k, float kick) {
    NswShip *s = &w->ship[i];
    const NswRace *r = &NSW_RACE[s->race];
    const NswSeg *g = &r->hull[k];
    NswV2 f = dir(s->a), l = v2(-f.y, f.x);
    for (int j = 0; j < NSW_MAX_DEBRIS; j++) {
        NswDebris *d = &w->debris[j];
        if (d->alive) continue;
        float mx = 0.5f * (g->x0 + g->x1), my = 0.5f * (g->y0 + g->y1);
        NswV2 c = add(s->p, add(mul(f, mx * r->scale), mul(l, my * r->scale)));
        d->alive = 1;
        d->p = c;
        d->v = add(mul(s->v, 0.5f), mul(norm(sub(c, s->p)), crand(w, 0.05f, kick)));
        d->a = s->a; d->w = crand(w, -6, 6);
        d->life = d->max_life = crand(w, 3.5f, 6.5f);
        d->hue = r->hue;
        d->seg.x0 = (g->x0 - mx) * r->scale; d->seg.y0 = (g->y0 - my) * r->scale;
        d->seg.x1 = (g->x1 - mx) * r->scale; d->seg.y1 = (g->y1 - my) * r->scale;
        d->seg.bright = g->bright;
        return;
    }
}

static void destroy(NswWorld *w, int i) {
    NswShip *s = &w->ship[i];
    const NswRace *r = &NSW_RACE[s->race];
    s->alive = 0;
    s->death_t = w->battle_t;
    /* Structural disassembly: the hull's own remaining segments fly apart. */
    for (int k = 0; k < r->nhull; k++)
        if (!(s->lost[k >> 6] >> (k & 63) & 1u)) shed(w, i, k, 0.35f);
    burst(w, s->p, mul(s->v, 0.4f), 60, 0.6f, r->hue, 1.4f);
    burst(w, s->p, v2(0, 0), 24, 0.25f, 0.13f, 2.0f);
    fx(w, FX_EXPLOSION, i, -1, s->p, s->p, 0.9f, r->hue);
    /* Hit-stop, then slow motion (wall time; the battle itself is unchanged). */
    w->freeze_t = 0.25f;
    w->slowmo_t = 1.5f;
    w->kills++;
    w->kill_p = s->p;
}

/* Below 30, 20 and 10% hull a piece of the lattice breaks away. Which piece
 * is cosmetic; that it happens is not. */
static void damage_stage(NswWorld *w, int i) {
    NswShip *s = &w->ship[i];
    const NswRace *r = &NSW_RACE[s->race];
    float hpf = s->hp / r->hp;
    int stage = hpf < 0.1f ? 3 : hpf < 0.2f ? 2 : hpf < 0.3f ? 1 : 0;
    while (s->dmg_stage < stage) {
        s->dmg_stage++;
        int cand[96], nc = 0;
        for (int k = 0; k < r->nhull && k < 96; k++)
            if (r->hull[k].bright < 0.9f && !(s->lost[k >> 6] >> (k & 63) & 1u)) cand[nc++] = k;
        if (!nc) break;
        int k = cand[(int)(urand(&w->fxrng) * (float)nc)];
        s->lost[k >> 6] |= (uint64_t)1 << (k & 63);
        shed(w, i, k, 0.2f);
        burst(w, s->p, mul(s->v, 0.5f), 10, 0.3f, 0.12f, 0.8f);
    }
}

static void hurt_by(NswWorld *w, int i, float dmg, NswV2 at, NswV2 push, int cause);
#define hurt(w, i, d, at, push) hurt_by(w, i, d, at, push, 0)
static void hurt_by(NswWorld *w, int i, float dmg, NswV2 at, NswV2 push, int cause) {
    NswShip *s = &w->ship[i];
    if (!s->alive) return;
    const NswRace *r = &NSW_RACE[s->race];
    w->calm_t = 0;
    w->hits++;
    if (s->shield_t > 0) {
        s->shield_cracks++;
        fx(w, FX_SHIELD_HIT, i, -1, at, at, 0.25f, r->hue);
        burst(w, at, v2(0, 0), 4, 0.3f, r->hue, 0.3f);
        return;
    }
    s->hp -= dmg;
    w->dmg_by[cause & 3] += dmg;
    s->hit_t = 0.25f;
    s->v = add(s->v, mul(push, 1.f / r->mass));
    burst(w, at, mul(push, 0.5f), 6, 0.35f, r->hue, 0.5f);
    if (s->hp <= 0) {
        if (cause == 2) w->bs.planet_kills++;
        destroy(w, i);
    } else damage_stage(w, i);
}

/* ---------------------------------------------------------------- weapons */
static NswV2 intercept(NswV2 from, NswV2 tp, NswV2 tv, float speed) {
    NswV2 d = sub(tp, from);
    float t = len(d) / fmaxf(speed, 0.1f);
    for (int k = 0; k < 2; k++) t = len(add(d, mul(tv, t))) / fmaxf(speed, 0.1f);
    return add(tp, mul(tv, fminf(t, 1.5f)));
}

static void fire_shot(NswWorld *w, int i, int type, float ang, float speed, float life, float dmg, int target) {
    NswShip *s = &w->ship[i];
    const NswRace *r = &NSW_RACE[s->race];
    int k = shot_new(w);
    if (k < 0) return;
    NswShot *q = &w->shot[k];
    q->type = type; q->owner = i; q->team = s->team; q->target = target;
    q->p = add(s->p, mul(dir(ang), r->scale * 1.05f));
    q->v = add(mul(s->v, 0.5f), mul(dir(ang), speed));
    q->life = life; q->dmg = dmg;
}

static int nearest_enemy(const NswWorld *w, int i, float *dist) {
    const NswShip *s = &w->ship[i];
    int best = -1;
    float bd = 1e9f;
    for (int j = 0; j < w->nships; j++) {
        const NswShip *o = &w->ship[j];
        if (!o->alive || o->team == s->team) continue;
        float d = len(sub(o->p, s->p));
        if (d < bd) { bd = d; best = j; }
    }
    if (dist) *dist = bd;
    return best;
}

/* Pilot i looks for ship j: updates i's memory of j unless j is cloaked, and
 * returns where i believes j is (a cloaked ship is where it was last seen). */
static NswV2 observe(NswWorld *w, int i, int j) {
    NswShip *s = &w->ship[i];
    const NswShip *o = &w->ship[j];
    if (o->cloak_t <= 0) s->last_seen[j] = o->p;
    return s->last_seen[j];
}

static void primary(NswWorld *w, int i, int tgt, NswV2 aim) {
    NswShip *s = &w->ship[i];
    const NswRace *r = &NSW_RACE[s->race];
    if (s->fire_t > 0 || s->energy < r->fire_cost || s->shield_t > 0) return;
    s->fire_t = r->fire_cd;
    s->energy -= r->fire_cost;
    float a = atan2f(aim.y - s->p.y, aim.x - s->p.x);
    switch (r->weapon) {
    case W_SPREAD:
        for (int k = -1; k <= 1; k++) fire_shot(w, i, SH_BOLT, a + 0.12f * k, 1.5f, 0.9f, 1.f, -1);
        break;
    case W_PLASMA: fire_shot(w, i, SH_PLASMA, a, 0.75f, 2.2f, 5.f, -1); break;
    case W_HOMING: fire_shot(w, i, SH_DART, s->a + frand(w, -0.6f, 0.6f), 0.9f, 3.f, 1.5f, tgt); break;
    case W_CHAIN: {
        const NswShip *o = &w->ship[tgt];
        if (len(sub(o->p, s->p)) > 0.56f) { s->fire_t = fminf(0.1f, r->fire_cd); s->energy += r->fire_cost; return; }
        fx(w, FX_CHAIN, i, tgt, s->p, o->p, 0.18f, r->hue);
        hurt_by(w, tgt, 2.6f, o->p, mul(norm(sub(o->p, s->p)), 0.05f), 3);
        for (int j = 0; j < w->nships; j++) {
            NswShip *x = &w->ship[j];
            if (j == tgt || !x->alive || x->team == s->team) continue;
            if (len(sub(x->p, o->p)) < 0.32f) {
                fx(w, FX_CHAIN, tgt, j, o->p, x->p, 0.18f, r->hue);
                hurt_by(w, j, 1.2f, x->p, v2(0, 0), 3);
            }
        }
        break;
    }
    case W_BOOMERANG: /* fired astern */
        fire_shot(w, i, SH_BOOMERANG, s->a + PI, 1.1f, 1.6f, 2.f, tgt);
        break;
    case W_FLAK:
        for (int k = 0; k < 5; k++) fire_shot(w, i, SH_PELLET, a + frand(w, -0.35f, 0.35f), frand(w, 1.1f, 1.4f), 0.35f, 0.8f, -1);
        break;
    case W_LANCE: fire_shot(w, i, SH_LANCE, a, 2.6f, 0.9f, 4.f, -1); break;
    }
}

static void special(NswWorld *w, int i, int tgt) {
    NswShip *s = &w->ship[i];
    const NswRace *r = &NSW_RACE[s->race];
    if (s->special_t > 0 || s->energy < r->special_cost) return;
    s->special_t = r->special_cd;
    s->energy -= r->special_cost;
    w->bs.specials[i]++;
    const NswShip *o = &w->ship[tgt];
    switch (r->special) {
    case S_BLINK: {
        /* Reappear beside the target, away from the planet. */
        NswV2 side = norm(v2(-(o->p.y), o->p.x));
        if (urand(&w->rng) < 0.5f) side = mul(side, -1.f);
        NswV2 np = add(o->p, mul(side, 0.34f));
        if (len(np) > w->arena_r * 0.85f) np = mul(norm(np), w->arena_r * 0.85f);
        if (len(np) < w->planet_r + 0.2f) np = mul(norm(np), w->planet_r + 0.2f);
        fx(w, FX_BLINK, i, -1, s->p, np, 0.4f, r->hue);
        burst(w, s->p, v2(0, 0), 12, 0.3f, r->hue, 0.4f);
        s->p = np;
        s->a = atan2f(o->p.y - np.y, o->p.x - np.x);
        burst(w, s->p, v2(0, 0), 12, 0.3f, r->hue, 0.4f);
        break;
    }
    case S_SHIELD: s->shield_t = 2.5f; s->shield_cracks = 0; break;
    case S_MINES:
        for (int k = 0; k < 3; k++) fire_shot(w, i, SH_MINE, s->a + PI + (k - 1) * 0.5f, 0.12f, 20.f, 4.f, -1);
        break;
    case S_DRONES:
        for (int k = 0; k < 3; k++) {
            int q = shot_new(w);
            if (q < 0) break;
            NswShot *d = &w->shot[q];
            d->type = SH_DRONE; d->owner = i; d->team = s->team; d->target = -1;
            d->p = add(s->p, mul(dir(2 * PI * k / 3), 0.13f));
            d->v = s->v; d->life = 12.f; d->dmg = 2.f;
            d->age = 2 * PI * k / 3;  /* orbit phase */
        }
        break;
    case S_CLOAK: s->cloak_t = 3.5f; break;
    case S_TRACTOR: s->tractor_t = 2.f; s->tractor_target = tgt; break;
    case S_REPULSE:
        fx(w, FX_REPULSE, i, -1, s->p, s->p, 0.45f, r->hue);
        for (int j = 0; j < w->nships; j++) {
            NswShip *x = &w->ship[j];
            if (!x->alive || x->team == s->team) continue;
            NswV2 d = sub(x->p, s->p);
            float l = len(d);
            if (l < 0.5f) x->v = add(x->v, mul(norm(d), 0.9f * (1.f - l / 0.5f) / NSW_RACE[x->race].mass));
        }
        for (int k = 0; k < NSW_MAX_SHOTS; k++) {
            NswShot *q = &w->shot[k];
            if (!q->alive || q->team == s->team) continue;
            NswV2 d = sub(q->p, s->p);
            if (len(d) < 0.5f) {
                q->v = mul(norm(d), len(q->v) + 0.3f);  /* turned back */
                q->team = s->team; q->owner = i; q->target = -1;
            }
        }
        break;
    }
}

/* ---------------------------------------------------------------- pilots */
static void pilot(NswWorld *w, int i, float dt) {
    NswShip *s = &w->ship[i];
    const NswRace *r = &NSW_RACE[s->race];
    float dist;
    int tgt = nearest_enemy(w, i, &dist);
    s->thrusting = 0;
    if (tgt < 0 || w->phase != PH_FIGHT) {
        /* Victory lap or idle: slow circle. */
        float want = s->a + 0.8f * dt;
        s->a = want;
        s->thrusting = len(s->v) < 0.18f;
        return;
    }
    const NswShip *o = &w->ship[tgt];
    NswV2 op = observe(w, i, tgt);
    NswV2 to = sub(op, s->p);
    dist = len(to);
    NswV2 td = norm(to), tang = mul(v2(-td.y, td.x), s->orbit_dir);
    NswV2 want = v2(0, 0);
    float R = r->range * (1.f - 0.35f * w->aggro);   /* a closing boundary presses them in */
    switch (r->tactic) {
    case T_KITE: case T_SNIPE: case T_FLEE:
        want = dist < R * 0.8f ? add(mul(td, -1.f), mul(tang, 0.6f)) : dist > R * 1.25f ? add(td, mul(tang, 0.4f)) : tang;
        break;
    case T_ORBIT: want = add(tang, mul(td, (dist - R) * 3.f)); break;
    case T_CLOSE: want = dist > R ? add(td, mul(tang, 0.3f)) : add(tang, mul(td, 0.2f)); break;
    case T_RAM: want = norm(sub(intercept(s->p, op, o->v, len(s->v) + 0.4f), s->p)); break;
    }
    /* A leash: nobody fights from across the arena. Past 1.1 every tactic
     * closes, so battles stay watchable (and frameable) at a readable size. */
    if (dist > 1.1f) want = add(td, mul(tang, 0.3f));
    /* Keep off the planet and inside the arena. */
    NswV2 fut = add(s->p, mul(s->v, 0.7f));
    float fr = len(fut);
    if (fr < w->planet_r + r->scale + 0.3f) want = add(want, mul(norm(fut), 5.f));
    /* Falling in: brake by pointing away from the planet when the fall is fast. */
    if (len(s->p) < w->planet_r + 0.35f && dot(s->v, norm(s->p)) < -0.25f) want = mul(norm(s->p), 6.f);
    if (len(s->p) > w->arena_r * 0.86f) want = add(want, mul(norm(s->p), -2.5f));
    /* Dodge shots that are about to hit. */
    for (int k = 0; k < NSW_MAX_SHOTS; k++) {
        const NswShot *q = &w->shot[k];
        if (!q->alive || q->team == s->team || q->type == SH_MINE) continue;
        NswV2 rp = sub(q->p, s->p), rv = sub(q->v, s->v);
        float vv = dot(rv, rv);
        if (vv < 1e-4f) continue;
        float tc = -dot(rp, rv) / vv;
        if (tc < 0 || tc > 0.6f) continue;
        NswV2 miss = add(rp, mul(rv, tc));
        if (len(miss) < r->scale + 0.06f) {
            NswV2 side = norm(v2(-rv.y, rv.x));
            if (dot(side, miss) > 0) side = mul(side, -1.f);
            want = add(want, mul(side, 2.2f));
        }
    }
    /* Attack runs: turn to aim; otherwise manoeuvre. */
    s->mode_t -= dt;
    if (s->mode_t <= 0) {
        s->mode = !s->mode;
        s->mode_t = s->mode ? frand(w, 1.2f, 2.6f) : frand(w, 1.0f, 2.2f) * (1.f - 0.6f * w->aggro);
        if (urand(&w->rng) < 0.2f) s->orbit_dir = -s->orbit_dir;
    }
    float speed = r->weapon == W_LANCE ? 2.6f : r->weapon == W_SPREAD ? 1.5f : r->weapon == W_PLASMA ? 0.75f : 1.2f;
    NswV2 aim = intercept(s->p, op, o->v, speed);
    float aim_a = atan2f(aim.y - s->p.y, aim.x - s->p.x);
    if (r->weapon == W_BOOMERANG) aim_a = wrap_angle(aim_a + PI);   /* fires astern */
    float move_a = atan2f(want.y, want.x);
    int attacking = s->mode && dist < (r->weapon == W_LANCE ? 1.4f : r->weapon == W_CHAIN ? 0.56f : 0.9f);
    float goal = attacking ? aim_a : move_a;
    float err = wrap_angle(goal - s->a);
    float turn = r->turn * dt;
    s->a = wrap_angle(s->a + clampf(err, -turn, turn));
    /* Thrust toward the movement goal when roughly facing it. */
    float move_err = fabsf(wrap_angle(move_a - s->a));
    if (!attacking || r->drag < 0.1f) s->thrusting = move_err < 0.7f;
    else s->thrusting = move_err < 0.4f && len(s->v) < r->vmax * 0.5f;
    if (r->drag < 0.1f && len(s->v) > r->vmax * 0.8f) s->thrusting = 0;  /* the drifter coasts */
    /* Fire. */
    float tol = r->weapon == W_SPREAD ? 0.3f : r->weapon == W_FLAK ? 0.5f : r->weapon == W_HOMING ? 1.2f :
                r->weapon == W_CHAIN ? 0.8f : 0.14f;
    float fire_range = r->weapon == W_LANCE ? 1.5f : r->weapon == W_FLAK ? 0.45f : r->weapon == W_CHAIN ? 0.56f : 1.0f;
    if (o->cloak_t <= 0 && dist < fire_range && fabsf(wrap_angle(aim_a - s->a)) < tol) primary(w, i, tgt, aim);
    /* Specials, each with its own judgement. */
    int incoming = 0;
    for (int k = 0; k < NSW_MAX_SHOTS; k++) {
        const NswShot *q = &w->shot[k];
        if (q->alive && q->team != s->team && q->type != SH_MINE && len(sub(q->p, s->p)) < 0.3f) incoming++;
    }
    float hpf = s->hp / r->hp;
    switch (r->special) {
    case S_BLINK: if ((dist > 0.9f && hpf > 0.4f) || (incoming >= 2 && hpf < 0.5f)) special(w, i, tgt); break;
    case S_SHIELD: if (incoming >= 1 && dist < 0.6f) special(w, i, tgt); break;
    case S_MINES: if (dist < 0.4f && dot(norm(sub(o->p, s->p)), dir(s->a)) < -0.3f) special(w, i, tgt); break;
    case S_DRONES: special(w, i, tgt); break;
    case S_CLOAK: if (hpf < 0.6f || incoming >= 2) special(w, i, tgt); break;
    case S_TRACTOR: if (dist > 0.25f && dist < 0.7f) special(w, i, tgt); break;
    case S_REPULSE: if (incoming >= 2 || dist < 0.25f) special(w, i, tgt); break;
    }
}

/* ---------------------------------------------------------------- physics */
static NswV2 gravity(const NswWorld *w, NswV2 p) {
    float r2 = dot(p, p) + 0.02f * 0.02f;
    return mul(p, -w->gm / (r2 * sqrtf(r2)));
}

static void step_ships(NswWorld *w, float dt) {
    for (int i = 0; i < w->nships; i++) {
        NswShip *s = &w->ship[i];
        if (!s->alive) continue;
        const NswRace *r = &NSW_RACE[s->race];
        pilot(w, i, dt);
        s->fire_t -= dt; s->special_t -= dt;
        s->shield_t -= dt; s->cloak_t -= dt; s->hit_t -= dt; s->planet_t -= dt;
        s->energy = fminf(r->energy, s->energy + r->regen * dt);
        NswV2 acc = gravity(w, s->p);
        if (s->thrusting) acc = add(acc, mul(dir(s->a), r->thrust / r->mass * 1.2f));
        if (s->tractor_t > 0 && s->tractor_target >= 0) {
            s->tractor_t -= dt;
            NswShip *o = &w->ship[s->tractor_target];
            if (o->alive && len(sub(o->p, s->p)) < 0.8f) {
                NswV2 d = norm(sub(s->p, o->p));
                o->v = add(o->v, mul(d, 0.8f * dt / NSW_RACE[o->race].mass));
                acc = add(acc, mul(d, -0.3f));
            } else s->tractor_t = 0;
        }
        s->v = add(s->v, mul(acc, dt));
        s->v = mul(s->v, expf(-r->drag * dt));
        float sp = len(s->v);
        if (sp > r->vmax * 1.6f) s->v = mul(s->v, r->vmax * 1.6f / sp);
        s->p = add(s->p, mul(s->v, dt));
        /* The arena boundary pushes back softly; the planet hurts. */
        float pr = len(s->p);
        if (pr > w->arena_r) {
            NswV2 n = mul(s->p, 1.f / pr);
            s->p = mul(n, w->arena_r);
            if (dot(s->v, n) > 0) s->v = sub(s->v, mul(n, 1.6f * dot(s->v, n)));
        }
        if (pr < w->planet_r + r->scale * 0.8f) {
            NswV2 n = norm(s->p);
            s->p = mul(n, w->planet_r + r->scale * 0.8f);
            float vn = dot(s->v, n);
            if (vn < 0) s->v = sub(s->v, mul(n, 1.7f * vn));
            if (s->planet_t <= 0) {   /* one knock per contact, not one per substep */
                hurt_by(w, i, 1.f + 2.f * fabsf(vn), s->p, v2(0, 0), 2);
                s->planet_t = 0.6f;
            }
        }
        if (s->thrusting && s->alive) {
            for (int k = 0; k < r->nengine; k++) {
                NswV2 f = dir(s->a), l = v2(-f.y, f.x);
                NswV2 e = add(s->p, add(mul(f, r->engine[k].x * r->scale), mul(l, r->engine[k].y * r->scale)));
                NswV2 back = mul(f, -1.f);
                if (urand(&w->fxrng) < 0.5f * (float)w->part_budget / 4.f)
                    part(w, e, add(s->v, mul(back, crand(w, 0.25f, 0.45f))), crand(w, 0.2f, 0.4f), r->hue + 0.05f, 0.015f, 0.45f);
            }
        }
        /* A badly hurt hull trails sparks. */
        if (s->dmg_stage > 0 && urand(&w->fxrng) < dt * 5.f * s->dmg_stage)
            part(w, s->p, add(mul(s->v, 0.6f), v2(crand(w, -0.1f, 0.1f), crand(w, -0.1f, 0.1f))), crand(w, 0.3f, 0.8f), 0.1f, 0.015f, 0.9f);
        s->trail_t -= dt;
        if (s->trail_t <= 0) {
            s->trail_t = 1.f / 40.f;
            if (s->ntrail == NSW_TRAIL) { memmove(s->trail, s->trail + 1, sizeof(NswV2) * (NSW_TRAIL - 1)); s->ntrail--; }
            s->trail[s->ntrail++] = s->p;
        }
        if (!isfinite(s->p.x) || !isfinite(s->p.y) || !isfinite(s->v.x) || !isfinite(s->v.y)) { w->nan_seen++; s->p = v2(0.5f, 0.5f); s->v = v2(0, 0); }
    }
    /* Ship on ship: a push, and damage by closing speed. Rams hurt more. */
    for (int i = 0; i < w->nships; i++)
        for (int j = i + 1; j < w->nships; j++) {
            NswShip *a = &w->ship[i], *b = &w->ship[j];
            if (!a->alive || !b->alive) continue;
            const NswRace *ra = &NSW_RACE[a->race], *rb = &NSW_RACE[b->race];
            NswV2 d = sub(b->p, a->p);
            float l = len(d), m = (ra->scale + rb->scale) * 0.85f;
            if (l >= m || l < 1e-5f) continue;
            NswV2 n = mul(d, 1.f / l);
            float vn = dot(sub(a->v, b->v), n);
            a->p = sub(a->p, mul(n, (m - l) * 0.5f));
            b->p = add(b->p, mul(n, (m - l) * 0.5f));
            if (vn > 0) {
                float im = 1.f / ra->mass + 1.f / rb->mass, jn = 1.6f * vn / im;
                a->v = sub(a->v, mul(n, jn / ra->mass));
                b->v = add(b->v, mul(n, jn / rb->mass));
                if (a->team != b->team) {
                    float base = 6.f * vn;
                    hurt_by(w, j, base * ra->ram_mult, add(a->p, mul(n, ra->scale)), v2(0, 0), 1);
                    hurt_by(w, i, base * rb->ram_mult, add(a->p, mul(n, ra->scale)), v2(0, 0), 1);
                }
            }
        }
}

static void step_shots(NswWorld *w, float dt) {
    for (int k = 0; k < NSW_MAX_SHOTS; k++) {
        NswShot *q = &w->shot[k];
        if (!q->alive) continue;
        q->age += dt;
        q->life -= dt;
        if (q->life <= 0) { q->alive = 0; continue; }
        NswShip *own = &w->ship[q->owner];
        switch (q->type) {
        case SH_DART: case SH_BOOMERANG:
            if (q->type == SH_DART || q->age > 0.55f) {
                int t = q->target;
                if (t < 0 || t >= w->nships || !w->ship[t].alive) { float d; t = nearest_enemy(w, q->owner, &d); q->target = t; }
                if (t >= 0) {
                    NswV2 want = norm(sub(w->ship[t].p, q->p));
                    float sp = len(q->v);
                    float turn = q->type == SH_DART ? 3.f : 2.2f;
                    NswV2 cur = norm(q->v);
                    float ang = atan2f(cur.y, cur.x), wa = atan2f(want.y, want.x);
                    ang += clampf(wrap_angle(wa - ang), -turn * dt, turn * dt);
                    q->v = mul(dir(ang), sp);
                }
            }
            break;
        case SH_MINE: q->v = mul(q->v, expf(-2.f * dt)); break;
        case SH_DRONE:
            if (!own->alive) { q->life = fminf(q->life, 0.5f); break; }
            if (q->target < 0) {
                q->age += 2.5f * dt;
                NswV2 want = add(own->p, mul(dir(q->age), 0.13f));
                q->v = mul(sub(want, q->p), 12.f);
                for (int j = 0; j < w->nships; j++)
                    if (w->ship[j].alive && w->ship[j].team != q->team && len(sub(w->ship[j].p, q->p)) < 0.3f) q->target = j;
            } else if (q->target < w->nships && w->ship[q->target].alive) {
                q->v = mul(norm(sub(w->ship[q->target].p, q->p)), 1.1f);
            } else q->target = -1;
            break;
        default: break;
        }
        if (q->type != SH_DRONE) q->v = add(q->v, mul(gravity(w, q->p), dt * (q->type == SH_LANCE ? 0.2f : 1.f)));
        q->p = add(q->p, mul(q->v, dt));
        if (q->nprev == 6) { memmove(q->prev, q->prev + 1, sizeof(NswV2) * 5); q->nprev--; }
        q->prev[q->nprev++] = q->p;
        float pr = len(q->p);
        if (pr < w->planet_r || pr > w->arena_r * 1.05f) {
            burst(w, q->p, v2(0, 0), 3, 0.15f, NSW_RACE[own->race].hue, 0.3f);
            q->alive = 0;
            continue;
        }
        float armed = q->type == SH_MINE ? (q->age > 1.f) : 1;
        for (int j = 0; j < w->nships && q->alive; j++) {
            NswShip *s = &w->ship[j];
            if (!s->alive || s->team == q->team || !armed) continue;
            float hit_r = NSW_RACE[s->race].scale * 0.85f + (q->type == SH_PLASMA ? 0.02f : q->type == SH_MINE ? 0.06f : 0.005f);
            if (len(sub(s->p, q->p)) < hit_r) {
                NswV2 push = mul(norm(q->v), q->type == SH_PLASMA ? 0.35f : 0.04f);
                hurt(w, j, q->dmg, q->p, push);
                if (q->type != SH_LANCE) q->alive = 0;   /* the lance passes through */
                else q->dmg *= 0.5f;
            }
        }
    }
}

static void step_fx(NswWorld *w, float dt) {
    for (int i = 0; i < NSW_MAX_PARTS; i++) {
        NswPart *q = &w->part[i];
        if (!q->alive) continue;
        q->life -= dt;
        if (q->life <= 0) { q->alive = 0; continue; }
        q->v = mul(q->v, expf(-1.2f * dt));
        q->p = add(q->p, mul(q->v, dt));
    }
    for (int i = 0; i < NSW_MAX_DEBRIS; i++) {
        NswDebris *d = &w->debris[i];
        if (!d->alive) continue;
        d->life -= dt;
        if (d->life <= 0 || len(d->p) < w->planet_r) { d->alive = 0; continue; }
        d->v = add(d->v, mul(gravity(w, d->p), dt));
        d->p = add(d->p, mul(d->v, dt));
        d->a += d->w * dt;
    }
    for (int i = 0; i < NSW_MAX_FX; i++) {
        NswFx *f = &w->fx[i];
        if (!f->alive) continue;
        f->t += dt;
        if (f->t >= f->dur) f->alive = 0;
        else if (f->type == FX_CHAIN && f->a >= 0 && f->b >= 0 && f->a < w->nships && f->b < w->nships) {
            f->p0 = w->ship[f->a].p;
            f->p1 = w->ship[f->b].p;
        }
    }
}

static void substep(NswWorld *w, float dt) {
    w->battle_t += dt;
    w->phase_t += dt;
    switch (w->phase) {
    case PH_INTRO:
        if (w->phase_t > 2.4f) { w->phase = PH_FIGHT; w->phase_t = 0; }
        break;
    case PH_FIGHT: {
        /* Once 12 s pass without a hit the boundary starts to close and the
         * pilots press in. It never reopens: a hit only restarts the count,
         * and aggro only grows, for the rest of the battle. */
        w->calm_t += dt;
        if (w->calm_t > w->bs.longest_calm) w->bs.longest_calm = w->calm_t;
        w->bs.sample_t += dt;
        if (w->bs.sample_t >= 0.5f) {
            w->bs.sample_t -= 0.5f;
            for (int i = 0; i < w->nships; i++) {
                const NswShip *a = &w->ship[i];
                if (!a->alive) continue;
                float nd = 1e9f, best = 0;
                for (int j = 0; j < w->nships; j++) {
                    const NswShip *b = &w->ship[j];
                    if (!b->alive || b->team == a->team) continue;
                    nd = fminf(nd, len(sub(b->p, a->p)));
                    best = fmaxf(best, b->hp / NSW_RACE[b->race].hp);
                }
                float gap = best - a->hp / NSW_RACE[a->race].hp;
                if (gap > w->bs.deficit[i]) w->bs.deficit[i] = gap;
                if (i == 0 && nd < 1e8f) { w->bs.dist_sum += nd; w->bs.dist_sq += nd * nd; w->bs.dist_n++; }
            }
        }
        if (w->calm_t > 12.f) {
            if (!w->closing) { w->closing = 1; w->contractions++; }
            w->aggro = fminf(1.f, w->aggro + dt / 15.f);
            if (w->arena_r > NSW_ARENA_MIN) w->arena_r = fmaxf(NSW_ARENA_MIN, w->arena_r - 0.04f * w->cfg.contract * dt);
        }
        int last = -1, n = teams_alive(w, &last);
        int timeout = w->battle_t > w->cfg.battle_max;
        if (n <= 1 || timeout) {
            if (timeout && n > 1) {
                /* Time: the side with the most hull left wins; the rest withdraw. */
                float best = -1;
                for (int i = 0; i < w->nships; i++)
                    if (w->ship[i].alive && w->ship[i].hp / NSW_RACE[w->ship[i].race].hp > best) {
                        best = w->ship[i].hp / NSW_RACE[w->ship[i].race].hp;
                        last = w->ship[i].team;
                    }
                for (int i = 0; i < w->nships; i++)
                    if (w->ship[i].alive && w->ship[i].team != last) destroy(w, i);
                w->timeouts++;
            }
            w->winner_team = n == 0 ? -1 : last;
            w->phase = PH_OUTRO;
            w->phase_t = 0;
            w->last_duration = w->battle_t;
            w->battles_done++;
            w->last_hash = nsw_state_hash(w);
            w->last_nwinners = 0;
            for (int i = 0; i < w->nships; i++)
                if (w->ship[i].team == w->winner_team) w->last_winner_races[w->last_nwinners++] = w->ship[i].race;
        }
        break;
    }
    case PH_OUTRO:
        if (w->phase_t > 4.5f) next_battle(w);
        break;
    }
    if (w->phase != PH_INTRO) step_ships(w, dt);
    step_shots(w, dt);
    step_fx(w, dt);
}

void nsw_step(NswWorld *w, float dt) {
    if (dt < 0) dt = 0;
    if (dt > 0.1f) dt = 0.1f;
    if (w->freeze_t > 0) w->freeze_t -= dt;
    else if (w->slowmo_t > 0) w->slowmo_t -= dt;
    w->acc += dt * nsw_time_scale(w);
    while (w->acc >= DT) {
        substep(w, DT);
        w->acc -= DT;
        w->t += DT;
    }
}
