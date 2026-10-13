/* SPDX-License-Identifier: Apache-2.0 */
/* magmasimplex_goo_sim.c - wax bodies in a heated liquid (engine=goo).
 *
 * Why bodies instead of the grid fluid: the grid engine moved well but
 * rendered as matte plastic, and within half a minute diffused into a
 * smooth colour wash with no distinct masses (docs/superpowers/rounds/
 * 2026-09-26-magmasimplex-fluid-evidence/). Here the wax is a set of soft
 * bodies plus the heated pool. The renderer (vendor/magmasimplex/goo.frag)
 * draws the iso-surface of their summed field, so coalescence, necking and
 * pinch-off follow from the geometry, and nothing can diffuse away.
 *
 * The thermal cycle, per body:
 *   - wax at the pool takes heat from the source, fastest over the middle;
 *   - warm wax is buoyant and rises slowly through a viscous liquid;
 *   - away from the source it relaxes toward the ambient temperature of its
 *     height, which falls toward the top, so it cools, loses buoyancy and
 *     sinks;
 *   - cold wax that settles on the pool drains back into it.
 * The pool buds new bodies: a body grows out of the pool, stays attached
 * for a moment (the field shows the neck), and breaks away. The pool buds
 * faster when few separate masses are in the column, the way a lamp that
 * has run for a while keeps several in play. Bodies that touch and drift
 * together coalesce (the smaller drains into the larger, conserving area);
 * large bodies split, the leading part pulling ahead until the neck
 * pinches off.
 */
#include "magmasimplex_goo_sim.h"
#include <math.h>
#include <string.h>

#define POOL_Y MS_GOO_POOL_Y
#define POOL_TOP (POOL_Y + 0.086f) /* where the pool's iso-surface sits */
/* goo.frag uses a kernel of support 1.8 r, whose iso-surface sits at 1.0 r
 * for a lone body; two bodies' surfaces bridge when their centers are
 * closer than about 1.21 (ra + rb). */
#define JOIN 1.21f
/* With a cap mass, bodies whose tops reach this height have fused into it. */
#define CAP_JOIN 0.86f
#define CAP_CEIL 0.80f

static float urand(uint32_t *s) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return (float)(*s & 0xffffffu) / 16777216.f;
}
static float frand(MsGooSim *s, float a, float b) { return a + (b - a) * urand(&s->rng); }

int ms_goo_sim_alive(const MsGooSim *s) {
    int n = 0;
    for (int i = 0; i < MS_GOO_MAXB; i++) n += s->b[i].alive;
    return n;
}
static int free_slot(const MsGooSim *s) {
    if (ms_goo_sim_alive(s) >= s->maxn) return -1;
    for (int i = 0; i < MS_GOO_MAXB; i++)
        if (!s->b[i].alive) return i;
    return -1;
}
static float new_hue(MsGooSim *s) { return (s->multicolour && frand(s, 0, 1) < 0.35f) ? 1.f : 0.f; }

static int find(int *p, int i) {
    while (p[i] != i) i = p[i] = p[p[i]];
    return i;
}

int ms_goo_sim_visible(const MsGooSim *s) {
    int par[MS_GOO_MAXB + 1];
    const int POOL = MS_GOO_MAXB;
    for (int i = 0; i <= MS_GOO_MAXB; i++) par[i] = i;
    /* A body still held by a neck or tower stalk is joined to the pool. */
    for (int j = 0; j < MS_GOO_MAXS; j++) {
        const MsGooStrand *st = &s->s[j];
        if (st->alive && st->kind <= 1 && st->body >= 0 && st->body < MS_GOO_MAXB && s->b[st->body].alive &&
            s->b[st->body].id == st->body_id && st->r1 > 0.004f)
            par[find(par, st->body)] = find(par, POOL);
    }
    for (int i = 0; i < MS_GOO_MAXB; i++) {
        const MsGooBody *a = &s->b[i];
        if (!a->alive || a->r < 0.02f) continue;
        if (a->y - a->r < POOL_TOP + 0.03f) par[find(par, i)] = find(par, POOL);
        if (s->cap && a->y + a->r > CAP_JOIN) par[find(par, i)] = find(par, POOL);
        for (int j = i + 1; j < MS_GOO_MAXB; j++) {
            const MsGooBody *c = &s->b[j];
            if (!c->alive || c->r < 0.02f) continue;
            float d = hypotf(a->x - c->x, a->y - c->y);
            if (d < JOIN * (a->r + c->r)) par[find(par, i)] = find(par, j);
        }
    }
    int seen[MS_GOO_MAXB + 1] = {0}, n = 0, pool = find(par, POOL);
    for (int i = 0; i < MS_GOO_MAXB; i++) {
        const MsGooBody *a = &s->b[i];
        if (!a->alive || a->r < 0.02f) continue;
        int r = find(par, i);
        if (r == pool || seen[r]) continue;
        seen[r] = 1;
        n++;
    }
    return n;
}

/* The lamp holds a fixed amount of wax. The bodies in the column may use
 * about this much area (screen units, height 2); the rest is in the pool. */
static float wax_budget(const MsGooSim *s) { return 0.62f * s->asp / 1.78f * s->wax_k; }
static float wax_in_column(const MsGooSim *s) {
    float a = 0;
    for (int i = 0; i < MS_GOO_MAXB; i++)
        if (s->b[i].alive) a += 3.14159265f * fmaxf(s->b[i].r, s->b[i].rt) * fmaxf(s->b[i].r, s->b[i].rt);
    return a;
}
float ms_goo_sim_wax_area(const MsGooSim *s) { return wax_in_column(s); }

static void bud(MsGooSim *s) {
    int k = free_slot(s);
    if (k < 0) return;
    float room = wax_budget(s) - wax_in_column(s);
    if (room < 3.14159265f * 0.07f * 0.07f) return;
    MsGooBody *b = &s->b[k];
    memset(b, 0, sizeof *b);
    b->alive = 1;
    b->mode = 1;
    b->partner = -1;
    b->x = frand(s, -0.75f, 0.75f) * (s->asp - 0.2f);
    b->y = POOL_Y + 0.02f;
    b->r = 0.012f;
    /* Mostly substantial bodies, some small ones: sizes vary like real wax. */
    /* Narrow screens get slightly smaller wax so the column does not jam. */
    float scale = fminf(1.f, fmaxf(0.6f, s->asp / 1.78f));
    float want = scale * (frand(s, 0, 1) < 0.3f ? frand(s, 0.06f, 0.1f) : frand(s, 0.12f, 0.21f));
    b->rt = fminf(want, sqrtf(room / 3.14159265f));
    b->T = 0.85f;
    b->hue = new_hue(s);
    b->visc = frand(s, 1.1f, 2.1f);
    b->wob = frand(s, 0, 6.2831853f);
    b->tex = s->next_tex++ & 63;
    b->id = ++s->next_id;
    s->buds++;
    /* The lifted wax stays joined to the pool by a neck that thins and snaps;
     * some buds grow into towers: a long stalk under a mushroom cap. */
    for (int j = 0; j < MS_GOO_MAXS; j++) {
        MsGooStrand *st = &s->s[j];
        if (st->alive) continue;
        memset(st, 0, sizeof *st);
        st->alive = 1;
        st->body = k;
        st->body_id = b->id;
        /* A lamp still warming up (startup) lifts thick, knobbly columns
         * that stand for a long time, as real lamps do in their first hour. */
        int warming = s->warm < 1.f;
        st->kind = (warming || (!s->few && frand(s, 0, 1) < 0.3f)) ? 1 : 0;
        st->x0 = b->x;
        st->life = warming ? frand(s, 30.f, 45.f)
                           : (st->kind == 1 ? frand(s, 14.f, 22.f) : (s->few ? frand(s, 3.f, 5.f) : frand(s, 5.f, 9.f)));
        st->base = warming ? frand(s, 0.6f, 0.75f) : (st->kind == 1 ? frand(s, 0.45f, 0.6f) : frand(s, 0.35f, 0.5f));
        /* Knobbly, but not a chain of pearls: strong, even lobes read as beads
         * on a string (operator photo IMG_8240); the shader also staggers them. */
        st->lobes = warming ? frand(s, 0.45f, 0.7f) : frand(s, 0.3f, 0.55f);
        st->T = 0.8f;
        break;
    }
}

static void strands_step(MsGooSim *s, float dt) {
    for (int j = 0; j < MS_GOO_MAXS; j++) {
        MsGooStrand *st = &s->s[j];
        if (!st->alive) continue;
        st->age += dt;
        if (st->kind <= 1) {
            MsGooBody *b = st->body >= 0 ? &s->b[st->body] : NULL;
            if (!b || !b->alive || b->id != st->body_id || b->mode >= 2) { st->alive = 0; continue; }
            /* Viscous thread: the radius falls linearly to the break, and as
             * it thins it beads (Rayleigh-Plateau), then snaps. */
            /* Few separate masses showing: let go soon (the neck is look,
             * the gate is the operator's requirement). */
            /* Eased, so the neck thins over a moment rather than jumping. */
            if (s->few && st->life > st->age + 1.5f) st->life = fmaxf(st->age + 1.5f, st->life - 4.f * dt);
            float f = 1.f - st->age / st->life;
            if (f <= 0.f) { st->alive = 0; s->snaps++; continue; }
            st->x1 = b->x;
            st->y1 = b->y - 0.55f * b->r;
            st->y0 = POOL_TOP - 0.04f;
            st->x0 += (b->x - st->x0) * 0.15f * dt;
            float len = st->y1 - st->y0;
            if (len > (st->kind == 1 ? 1.1f : 0.8f)) { st->alive = 0; s->snaps++; continue; }
            st->r1 = b->r * st->base * (st->kind == 1 ? 0.55f + 0.45f * f : f);
            st->r0 = st->r1 * (st->kind == 1 ? 1.9f : 1.3f);
            st->bead = st->kind == 1 ? st->lobes : 0.7f * (1.f - f) * (1.f - f);
            st->T = b->T;
            /* A tower's stalk holds its cap back: the mushroom rises slowly. */
            if (st->kind == 1) b->vy *= expf(-0.9f * f * dt);
        } else {
            /* Stalactite: grows down from the cap, then drops its tip as a
             * cold drip and retracts. */
            if (!st->retracting) {
                st->len = fminf(st->len + 0.011f * dt, st->len_t);
                if (st->len >= st->len_t) {
                    int k = free_slot(s);
                    float room = wax_budget(s) - wax_in_column(s);
                    if (k >= 0 && room > 3.14159265f * 0.06f * 0.06f) {
                        MsGooBody *b = &s->b[k];
                        memset(b, 0, sizeof *b);
                        b->alive = 1;
                        b->partner = -1;
                        b->x = st->x1;
                        b->y = st->y1 - 0.02f;
                        b->r = st->r1 * 1.2f;
                        b->rt = fminf(frand(s, 0.06f, 0.1f), sqrtf(room / 3.14159265f));
                        b->T = 0.12f;
                        b->vy = -0.01f;
                        b->hue = new_hue(s);
                        b->visc = frand(s, 1.1f, 2.1f);
                        b->wob = frand(s, 0, 6.2831853f);
                        b->tex = s->next_tex++ & 63;
                        b->id = ++s->next_id;
                        s->drips++;
                    }
                    /* Dropped, or no room for a drip: retract either way. */
                    st->retracting = 1;
                }
            } else {
                st->len -= 0.05f * dt;
                st->r1 *= expf(-0.4f * dt);
                if (st->len < 0.02f) { st->alive = 0; continue; }
            }
            st->x1 = st->x0 + 0.02f * sinf(st->age * 0.13f + (float)j);
            st->y1 = st->y0 - st->len;
        }
    }
    if (s->cap && s->t >= s->next_drip) {
        for (int j = 0; j < MS_GOO_MAXS; j++) {
            MsGooStrand *st = &s->s[j];
            if (st->alive) continue;
            memset(st, 0, sizeof *st);
            st->alive = 1;
            st->kind = 2;
            st->body = -1;
            st->x0 = frand(s, -0.8f, 0.8f) * (s->asp - 0.2f);
            st->y0 = 0.90f;
            st->len_t = frand(s, 0.22f, 0.5f);
            st->r0 = frand(s, 0.03f, 0.05f);
            st->r1 = st->r0 * 1.35f;
            /* A smooth drip, barely knuckled (the reference caps shed
             * smooth drops; strong beads drew a stack of rings). */
            st->bead = frand(s, 0.1f, 0.35f);
            st->T = 0.15f;
            break;
        }
        s->next_drip = s->t + frand(s, 18.f, 40.f);
    }
}

void ms_goo_sim_step(MsGooSim *s, float dt) {
    float t = (float)s->t;
    /* Checked twice a second: are fewer than three separate masses showing? */
    if (s->t >= s->next_check) {
        s->few = ms_goo_sim_visible(s) < 3;
        s->next_check = s->t + 0.5;
    }
    int few = s->few;
    float xb = s->asp - 0.04f;
    for (int i = 0; i < MS_GOO_MAXB; i++) {
        MsGooBody *b = &s->b[i];
        if (!b->alive) continue;
        if (b->y - b->r < POOL_Y + 0.12f) {
            float hot = 0.3f + 0.7f * expf(-b->x * b->x * 0.5f);
            b->T += (1.f - b->T) * 0.22f * hot * dt * s->heat_k * s->warm;
        } else {
            float amb = 0.3f - 0.25f * (b->y - POOL_Y) / 1.8f;
            b->T += (amb - b->T) * 0.035f * dt;
        }
        float ay = 0.55f * (b->T - 0.5f);
        float ax = 0.03f * sinf(b->y * 2.1f + t * 0.07f + b->wob);
        if (b->mode == 1) {
            b->vx = 0;
            b->vy = fminf(b->vy + ay * dt, 0.012f);
            if (b->r > 0.9f * b->rt) b->mode = 0;
        } else {
            b->vx += ax * dt;
            b->vy += ay * dt;
        }
        if (b->mode == 2) {
            const MsGooBody *q = (b->partner >= 0 && b->partner < MS_GOO_MAXB) ? &s->b[b->partner] : NULL;
            if (!q || !q->alive) { b->mode = 0; b->rt = b->r; b->partner = -1; }
            else { b->vx += (q->x - b->x) * 0.9f * dt; b->vy += (q->y - b->y) * 0.9f * dt; }
        }
        float damp = expf(-b->visc * s->visc_k * dt);
        b->vx *= damp;
        b->vy *= damp;
        if (b->x < -xb + b->r) b->vx += 0.3f * dt;
        if (b->x > xb - b->r) b->vx -= 0.3f * dt;
        if (b->y > (s->cap ? CAP_CEIL : 0.97f) - 0.5f * b->r) b->vy -= 0.4f * dt;
        if (b->mode == 0 && b->y < POOL_Y + 0.03f && b->vy < 0 && b->T < 0.6f) {
            b->mode = 3;
            b->rt = 0;
            s->drains++;
        }
        float rate = b->mode == 1 ? 0.5f : 0.8f;
        b->r += (b->rt - b->r) * (1.f - expf(-rate * dt));
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        if (b->y < POOL_Y - 0.06f) { b->y = POOL_Y - 0.06f; b->vy = 0; }
        if ((b->mode == 2 || b->mode == 3) && b->r < 0.008f) b->alive = 0;
    }
    /* A body draining into a partner that has gone (and whose slot may be
     * reused later in this step) stops draining and keeps its size. */
    for (int i = 0; i < MS_GOO_MAXB; i++) {
        MsGooBody *a = &s->b[i];
        if (a->alive && a->mode == 2 && (a->partner < 0 || !s->b[a->partner].alive)) {
            a->mode = 0;
            a->rt = a->r;
            a->partner = -1;
        }
    }
    for (int i = 0; i < MS_GOO_MAXB; i++) {
        MsGooBody *a = &s->b[i];
        if (!a->alive) continue;
        for (int j = i + 1; j < MS_GOO_MAXB; j++) {
            MsGooBody *c = &s->b[j];
            if (!c->alive) continue;
            float dx = c->x - a->x, dy = c->y - a->y;
            float d = sqrtf(dx * dx + dy * dy) + 1e-6f, sum = a->r + c->r;
            /* Bodies that meet mostly slide past each other: the liquid between
             * them has to get out of the way first, which pushes them apart
             * sideways. Only slow, similar bodies stay long enough to fuse. */
            if (d < 1.35f * sum && a->mode != 2 && c->mode != 2) {
                float k = (1.35f * sum - d) / (1.35f * sum);
                float side = dx >= 0 ? 1.f : -1.f;
                float push = k * (few ? 0.35f : 0.12f) * dt;
                a->vx -= side * push; c->vx += side * push;
                /* ...and past each other vertically: the warmer rises past the
                 * cooler, so bodies near neutral buoyancy do not stall side by
                 * side in one band and read as a single mass. */
                float up = tanhf((a->T - c->T) * 8.f);
                a->vy += up * push * 0.8f; c->vy -= up * push * 0.8f;
                if (d < 0.8f * sum) {
                    float p2 = (0.8f * sum - d) * 0.9f * dt / d;
                    a->vx -= dx * p2; a->vy -= dy * p2;
                    c->vx += dx * p2; c->vy += dy * p2;
                }
            }
            if (a->mode == 0 && c->mode == 0 && d < 0.8f * sum) {
                float dv = hypotf(a->vx - c->vx, a->vy - c->vy);
                float merged = sqrtf(a->r * a->r + c->r * c->r);
                /* Real lamps coalesce often (every few minutes a pair fuses);
                 * at 0.03/s no merge was seen in a 5-minute run. 0.12/s cost
                 * one randomize draw its third mass at 60 s; 0.08 keeps the
                 * gate (1000 draws) while merging several times an hour. */
                if (dv < 0.07f && merged < 0.25f && fabsf(a->T - c->T) < 0.25f && urand(&s->rng) < 0.08f * dt) {
                    MsGooBody *big = a->r >= c->r ? a : c, *small = a->r >= c->r ? c : a;
                    float wb = big->r * big->r, ws = small->r * small->r;
                    big->hue = (big->hue * wb + small->hue * ws) / (wb + ws);
                    big->T = (big->T * wb + small->T * ws) / (wb + ws);
                    big->rt = merged;
                    small->mode = 2;
                    small->rt = 0;
                    small->partner = (int)(big - s->b);
                    s->merges++;
                }
            }
        }
    }
    /* When few masses show, big bodies stretch until they split. */
    for (int i = 0; i < MS_GOO_MAXB; i++) {
        MsGooBody *a = &s->b[i];
        float big = (few ? 0.13f : 0.19f) * fminf(1.f, fmaxf(0.6f, s->asp / 1.78f));
        /* rt, not r: a body that just split keeps its size for a moment
         * while it relaxes, and must not split again meanwhile. */
        if (!a->alive || a->mode != 0 || a->rt < big) continue;
        float sp = hypotf(a->vx, a->vy);
        if (sp < 0.015f || urand(&s->rng) >= (few ? 0.5f : 0.08f) * dt) continue;
        int k = free_slot(s);
        if (k < 0) break;
        MsGooBody *c = &s->b[k];
        float dx = a->vx / sp, dy = a->vy / sp;
        *c = *a;
        c->x += dx * 0.3f * a->r;
        c->y += dy * 0.3f * a->r;
        c->r = 0.02f;
        c->rt = a->r * 0.7071f;
        c->vx += dx * 0.03f;
        c->vy += dy * 0.03f;
        c->wob = frand(s, 0, 6.2831853f);
        c->tex = s->next_tex++ & 63;
        c->id = ++s->next_id;
        c->partner = -1;
        a->rt = a->r * 0.7071f;
        s->splits++;
    }
    /* Startup: the slab warms; nothing buds until it is soft enough. */
    if (s->warm < 1.f) s->warm = fminf(1.f, s->warm + dt / fmaxf(s->warm_s, 1.f));
    /* Few separate masses in the column: the pool buds again soon. */
    if (few && s->next_bud > s->t + 1.0 && s->warm >= 1.f) s->next_bud = s->t + 1.0;
    if (s->t >= s->next_bud && s->warm > 0.35f) {
        int budding = 0;
        for (int i = 0; i < MS_GOO_MAXB; i++) budding += s->b[i].alive && s->b[i].mode == 1;
        int vis = ms_goo_sim_visible(s);
        if (budding < (vis < 3 ? 2 : 1)) bud(s);
        s->next_bud = s->t + (vis < 4 ? frand(s, 2.5f, 5.f) : frand(s, 7.f, 13.f));
    }
    strands_step(s, dt);
    s->t += dt;
}

void ms_goo_sim_init(MsGooSim *s, uint32_t seed, int maxn, int multicolour, float asp, float prewarm_s) {
    ms_goo_sim_init_cap(s, seed, maxn, multicolour, asp, prewarm_s, 0);
}

void ms_goo_sim_init_cap(MsGooSim *s, uint32_t seed, int maxn, int multicolour, float asp, float prewarm_s, int cap) {
    memset(s, 0, sizeof *s);
    s->cap = cap;
    s->visc_k = s->heat_k = s->wax_k = 1.f;
    s->warm = 1.f;
    s->maxn = maxn > MS_GOO_MAXB ? MS_GOO_MAXB : maxn;
    s->multicolour = multicolour;
    s->rng = seed ? seed : 0x9E3779B9u;
    s->asp = asp;
    int n0 = 5 + (int)(frand(s, 0, 1) * 4);
    for (int i = 0; i < n0 && i < s->maxn; i++) {
        MsGooBody *b = &s->b[i];
        b->alive = 1;
        b->partner = -1;
        b->x = frand(s, -0.8f, 0.8f) * (asp - 0.2f);
        b->y = frand(s, -0.5f, cap ? 0.6f : 0.8f);
        b->r = b->rt = frand(s, 0.07f, 0.18f) * fminf(1.f, fmaxf(0.6f, asp / 1.78f));
        b->T = frand(s, 0.3f, 0.8f);
        b->hue = new_hue(s);
        b->visc = frand(s, 1.1f, 2.1f);
        b->wob = frand(s, 0, 6.2831853f);
        b->tex = s->next_tex++ & 63;
        b->id = ++s->next_id;
    }
    s->next_bud = 1.0;
    s->next_drip = 5.0;
    for (float t = 0; t < prewarm_s; t += 1.f / 30.f) ms_goo_sim_step(s, 1.f / 30.f);
}

void ms_goo_sim_init_cold(MsGooSim *s, uint32_t seed, int maxn, int multicolour, float asp, int cap, float warm_s) {
    memset(s, 0, sizeof *s);
    s->cap = cap;
    s->visc_k = s->heat_k = s->wax_k = 1.f;
    s->maxn = maxn > MS_GOO_MAXB ? MS_GOO_MAXB : maxn;
    s->multicolour = multicolour;
    s->rng = seed ? seed : 0x9E3779B9u;
    s->asp = asp;
    s->warm = 0.f;
    s->warm_s = warm_s;
    s->next_bud = 1.0;
    s->next_drip = warm_s; /* the cap forms from wax that has risen */
}
