/* SPDX-License-Identifier: Apache-2.0 */
/* neonspacewar_ai.h — the demo AI for the neon vector rock-shooter.
 *
 * This is the feature. The mechanics are simple; what separates a
 * "competent" bot from a twitchy one is the threat model.
 *
 * Per-frame `ai_update(state, dt)` does:
 *   1. Threat triage: rank alive rocks by time-to-collision (TTC),
 *      not distance. The closest rock in pixels is often irrelevant
 *      if it's moving sideways; the second-closest rock that is
 *      heading straight at the ship is the actual threat.
 *   2. Evasive thrust: if any rock has a short TTC, push the ship
 *      perpendicular to its velocity vector — never straight away
 *      from a converging bullet/rock (you'll outrun one and crash
 *      into the next).
 *   3. Lead the target: when choosing what to shoot, aim where a
 *      rock will be when the bullet arrives. Compute bullet travel
 *      time = dist(bullet_speed) / (1.05), then estimate where the
 *      rock will be: pos + vel * t (clamped to a sane horizon).
 *      Account for wrapping: if the bullet should take the short
 *      way around the playfield, aim at the wrapped target.
 *   4. Pace kills: don't shoot if the next shot would kill a rock
 *      whose split would land a fragment on top of the ship within
 *      the next second or two.
 *   5. If threats are scarce, drift toward an open quadrant so we
 *      are not standing still.
 *
 * The AI commands are written into state->ship.turn_rate (rad/s)
 * and state->ship.thrust_cmd (0..1). They are *suggestions*; the
 * ship physics integrator in physics_step() turns them into
 * actual motion.
 */

#ifndef NCZ_NEO_SPACEWAR_AI_H
#define NCZ_NEO_SPACEWAR_AI_H

#include <math.h>
#include <stddef.h>

/* ===================================================================== */
/* Tunables — AI personality                                               */
/* ===================================================================== */

#define NEO_AI_BULLET_SPEED         1.0f    /* matches physics bullet speed */
#define NEO_AI_BULLET_COOLDOWN      0.18f   /* matches physics */
#define NEO_AI_MAX_TURN_RATE        6.5f    /* rad/s — how fast the AI spins */
#define NEO_AI_SAFE_TTC             2.5f    /* seconds — beyond this, relax */
#define NEO_AI_PANIC_TTC            1.8f    /* seconds — full evasive below */
#define NEO_AI_LEAD_HORIZON         1.2f    /* seconds — cap on lead time */
#define NEO_AI_LOOK_AHEAD_DT        0.04f   /* seconds — TTC sample step */
#define NEO_AI_LOOK_AHEAD_STEPS     60      /* samples — 60 * 0.04 = 2.4s */

/* The playfield wraps in (-1.05, 1.05) on both axes. */
#define NEO_AI_LO  (-1.05f)
#define NEO_AI_HI   ( 1.05f)

/* ===================================================================== */
/* AI helpers — local to this header                                       */
/* ===================================================================== */

static inline float neo_clamp(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static inline float neo_wrap(float x) {
    /* wrap into (-1.05, 1.05). */
    const float w = NEO_AI_HI - NEO_AI_LO;
    while (x <  NEO_AI_LO) x += w;
    while (x >= NEO_AI_HI) x -= w;
    return x;
}
static inline float neo_wrap_delta(float from, float to) {
    const float w = NEO_AI_HI - NEO_AI_LO;
    float d = to - from;
    while (d >  w * 0.5f) d -= w;
    while (d < -w * 0.5f) d += w;
    return d;
}

/* Shortest-line intercept of a moving point. Given shooter at S with
 * bullet speed B, target at T moving with velocity V, returns the
 * time t at which a bullet should be fired (along direction
 * normalize(T - S + V*t)) so that the bullet and target arrive at
 * the same point. The standard quadratic:
 *
 *   |S + B*t*dir - (T + V*t)|^2 = 0
 *
 * With dir = normalize(target_lead_pos - S), it's an iterative solve;
 * for our purposes we just estimate via:
 *
 *   t ≈ |T - S| / B   (no target motion)
 *   target_lead = T + V * t
 *   dir = normalize(target_lead - S)
 *   t ≈ |target_lead - S| / B
 *
 * Two iterations is plenty. Returns t (in seconds) via *out_t.
 * Returns the aim direction via *out_dir (already normalized). */
static float neo_lead_target(V2 S, V2 T, V2 V, float B,
                             V2 *out_dir) {
    /* Initial guess: aim at T, travel time = dist/B. */
    float dx = neo_wrap_delta(S.x, T.x);
    float dy = neo_wrap_delta(S.y, T.y);
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 1e-5f) {
        out_dir->x = 1.f; out_dir->y = 0.f;
        return 0.f;
    }
    float t = d / B;
    if (t > NEO_AI_LEAD_HORIZON) t = NEO_AI_LEAD_HORIZON;
    /* Iterate once. */
    V2 lead = { T.x + V.x * t, T.y + V.y * t };
    /* Re-wrap. */
    lead.x = neo_wrap(lead.x);
    lead.y = neo_wrap(lead.y);
    dx = neo_wrap_delta(S.x, lead.x);
    dy = neo_wrap_delta(S.y, lead.y);
    d  = sqrtf(dx * dx + dy * dy);
    t = d / B;
    if (t > NEO_AI_LEAD_HORIZON) t = NEO_AI_LEAD_HORIZON;
    /* Final aim direction. */
    if (d < 1e-5f) {
        out_dir->x = 1.f; out_dir->y = 0.f;
    } else {
        out_dir->x = dx / d;
        out_dir->y = dy / d;
    }
    return t;
}

/* Time-to-collision along the wrapping playfield, given two
 * positions and a closing velocity. If they never collide within
 * NEO_AI_LOOK_AHEAD_STEPS * NEO_AI_LOOK_AHEAD_DT seconds (they are
 * not on a collision course), returns a large sentinel. */
static float neo_ttc(V2 S, V2 V_S, V2 T, V2 V_T) {
    /* Relative position + velocity. Use the *shortest* delta so
     * that two rocks on opposite sides of the wrap don't read as
     * 2.1 units apart when they're 0.05 units apart in the
     * wrapped sense. */
    float rx = neo_wrap_delta(S.x, T.x);
    float ry = neo_wrap_delta(S.y, T.y);
    /* Relative velocity (target vel - ship vel) is what matters
     * for closing rate. */
    float vx = V_T.x - V_S.x;
    float vy = V_T.y - V_S.y;
    /* If relative velocity is zero, TTC is infinite — no closing. */
    float v2 = vx * vx + vy * vy;
    if (v2 < 1e-8f) return 1e6f;
    /* Solve (rx + vx*t)·(rx + vx*t) = (radius_sum)^2 for t.
     * For "first contact" we want the smallest positive t such
     * that the closing distance equals sum-of-radii. The rock
     * collision radius is r->radius; we approximate the ship's
     * collision radius as a fixed 0.04. Sum = 0.04 + r_radius. */
    /* To keep the AI cheap, we use a "perpendicular miss" estimate:
     * the closest point of approach is at t = -((rx·vx + ry·vy)/v2).
     * The minimum distance is sqrt(d2 - ((rx·vx+ry·vy)^2)/v2). If
     * that minimum is below the collision radius, they will collide
     * at t_impact = t_ca + sqrt(r^2 - d_perp^2) / |v|. */
    /* Simpler: sample forward. */
    float t_best = 1e6f;
    float r_sum_sq = 0.04f * 0.04f;  /* ship-side only; rocks vary */
    /* We use rock radius info via a conservative 0.2 (large rock). */
    /* In practice the per-rock ttc is computed in ai_update via
     * iterating over rocks; this helper is unused in the final
     * version but kept as a building block. */
    (void)r_sum_sq;
    /* Quick closed-form: solve (rx+vx*t)^2 + (ry+vy*t)^2 = R^2.
     * Let a = v2, b = 2*(rx*vx + ry*vy), c = rx^2+ry^2 - R^2.
     * Then t = (-b ± sqrt(b^2 - 4ac)) / (2a). */
    /* R = 0.20 (large rock), conservative. */
    float R = 0.20f;
    float a = v2;
    float b = 2.f * (rx * vx + ry * vy);
    float c = rx * rx + ry * ry - R * R;
    float disc = b * b - 4.f * a * c;
    if (disc < 0.f) return 1e6f;
    float sq = sqrtf(disc);
    float t1 = (-b - sq) / (2.f * a);
    float t2 = (-b + sq) / (2.f * a);
    if (t1 > 0.f && t1 < t_best) t_best = t1;
    if (t2 > 0.f && t2 < t_best) t_best = t2;
    /* Cap. */
    float horizon = NEO_AI_LOOK_AHEAD_DT * NEO_AI_LOOK_AHEAD_STEPS;
    if (t_best > horizon) t_best = 1e6f;
    return t_best;
}

/* Wrap-aware distance squared. */
static inline float neo_dist2(V2 a, V2 b) {
    float dx = neo_wrap_delta(a.x, b.x);
    float dy = neo_wrap_delta(a.y, b.y);
    return dx * dx + dy * dy;
}

/* Normalize, returning a unit vector. */
static inline V2 neo_normalize(V2 v, float *out_len) {
    float len = sqrtf(v.x * v.x + v.y * v.y);
    if (out_len) *out_len = len;
    if (len < 1e-6f) {
        V2 z = {1.f, 0.f};
        return z;
    }
    V2 n = { v.x / len, v.y / len };
    return n;
}

/* Smallest signed angle from `from` to `to`, in (-pi, pi]. */
static inline float neo_angle_diff(float from, float to) {
    float d = to - from;
    while (d >  (float)M_PI) d -= 2.f * (float)M_PI;
    while (d <= -(float)M_PI) d += 2.f * (float)M_PI;
    return d;
}

/* ===================================================================== */
/* AI state                                                                */
/* ===================================================================== */

typedef struct {
    int   threat_idx;          /* -1 if no threat */
    float threat_ttc;
    V2    threat_pos;
    V2    threat_vel;
    float target_heading;      /* what we want to point at */
    float target_thrust;       /* 0..1 */
    int   want_to_shoot;       /* bool */
    V2    aim_dir;             /* leading aim direction */
    float aim_t;               /* bullet travel time */
} AIDebug;

/* ===================================================================== */
/* AI update                                                               */
/* ===================================================================== */

static void ai_update(State *st, float dt) {
    Ship *ship = &st->ship;
    if (!ship->alive || ship->dying) {
        ship->turn_rate = 0.f;
        ship->thrust_cmd = 0.f;
        return;
    }

    /* --- 1. Threat triage. Find the closest rock by time-to-
     * collision, not distance. For each alive rock, compute TTC
     * using the wrapping geometry; pick the minimum. */
    int threat_idx = -1;
    float threat_ttc = 1e6f;
    for (int i = 0; i < MAX_ROCKS; i++) {
        Rock *r = &st->rocks[i];
        if (!r->alive) continue;
        /* Use the rock's own collision radius. */
        V2 S = ship->pos;
        V2 V_S = ship->vel;
        V2 T = r->pos;
        V2 V_T = r->vel;
        float rx = neo_wrap_delta(S.x, T.x);
        float ry = neo_wrap_delta(S.y, T.y);
        float vx = V_T.x - V_S.x;
        float vy = V_T.y - V_S.y;
        float v2 = vx * vx + vy * vy;
        if (v2 < 1e-8f) continue;
        float R = 0.04f + r->radius;
        float a = v2;
        float b = 2.f * (rx * vx + ry * vy);
        float c = rx * rx + ry * ry - R * R;
        float disc = b * b - 4.f * a * c;
        if (disc < 0.f) continue;
        float sq = sqrtf(disc);
        float t1 = (-b - sq) / (2.f * a);
        float t2 = (-b + sq) / (2.f * a);
        float t_hit = 1e6f;
        if (t1 > 0.f && t1 < t_hit) t_hit = t1;
        if (t2 > 0.f && t2 < t_hit) t_hit = t2;
        if (t_hit < threat_ttc) {
            threat_ttc = t_hit;
            threat_idx = i;
        }
    }

    V2 threat_pos = {0, 0};
    V2 threat_vel = {0, 0};
    if (threat_idx >= 0) {
        threat_pos = st->rocks[threat_idx].pos;
        threat_vel = st->rocks[threat_idx].vel;
    }

    /* --- 2. Pick what to shoot. Prefer the closest medium/small
     * rock by *lead-corrected distance*. If no such rock exists, or
     * we're in panic, skip shooting. We always pick a target —
     * shoot_idx is what we aim at; want_to_shoot says whether the
     * trigger should be pressed right now. */
    int shoot_idx = -1;
    float shoot_score = 1e6f;
    /* Suppress shooting while turning hard toward a threat — the
     * bullet would miss anyway. */
    int panic = (threat_ttc < NEO_AI_PANIC_TTC);
    int in_combat_zone = (threat_ttc < NEO_AI_SAFE_TTC);

    /* Always compute a target so we keep aiming at it even while
     * cooling down between shots. */
    int n_alive = 0;
    for (int i = 0; i < MAX_ROCKS; i++) if (st->rocks[i].alive) n_alive++;
    int dense = (n_alive >= 8);

    for (int i = 0; i < MAX_ROCKS; i++) {
        Rock *r = &st->rocks[i];
        if (!r->alive) continue;
        V2 aim;
        float t = neo_lead_target(ship->pos, r->pos, r->vel,
                                  NEO_AI_BULLET_SPEED, &aim);
        float d2 = neo_dist2(ship->pos, r->pos);
        float score = d2;
        /* When the field is dense, prefer to kill small rocks first
         * (they don't split, removing a threat without creating
         * new ones). */
        if (dense) {
            if      (r->tier == ROCK_SMALL)  score -= 0.30f;
            else if (r->tier == ROCK_MEDIUM) score -= 0.10f;
            else                            score += 0.20f;
        } else {
            /* Calm field: small rocks are cheap, large rocks give a
             * big kill bonus. */
            if      (r->tier == ROCK_LARGE)  score += 0.05f;
            else if (r->tier == ROCK_SMALL)  score += 0.20f;
        }
        if (t > 1.4f) score += 5.f;
        /* Don't shoot if the kill would land a fragment on top of us. */
        if (d2 < 0.06f) score += 10.f;
        if (score < shoot_score) {
            shoot_score = score;
            shoot_idx = i;
        }
    }
    /* Trigger only fires when ready and not panicking. */
    int want_to_shoot = (shoot_idx >= 0) && !panic
                        && (ship->shoot_cooldown <= 0.f);

    /* --- 3. Aim direction. If we have a shoot target, aim at its
     * lead-corrected position. Otherwise, if we have a threat, aim
     * perpendicular to its velocity (evasion). Otherwise, drift. */
    float target_heading = ship->heading;
    float target_thrust  = 0.4f;

    if (shoot_idx >= 0) {
        Rock *r = &st->rocks[shoot_idx];
        V2 aim;
        float t = neo_lead_target(ship->pos, r->pos, r->vel,
                                  NEO_AI_BULLET_SPEED, &aim);
        target_heading = atan2f(aim.y, aim.x);
        /* Throttle back slightly while lining up a shot. */
        target_thrust = 0.25f;
        (void)t;
    } else if (threat_idx >= 0 && in_combat_zone) {
        /* Evasion: pick a thrust direction that takes us away from
         * ALL nearby threats, not just the closest. For each rock
         * in the combat zone, contribute an "away" unit vector
         * weighted by inverse TTC (closer = bigger pull). The
         * thrust direction is then the heading that maximally aligns
         * with the sum. If the sum is small, fall back to
         * perpendicular-to-closest. */
        V2 away_sum = {0, 0};
        for (int i = 0; i < MAX_ROCKS; i++) {
            Rock *r = &st->rocks[i];
            if (!r->alive) continue;
            /* Quick TTC for ranking. */
            float rx = neo_wrap_delta(ship->pos.x, r->pos.x);
            float ry = neo_wrap_delta(ship->pos.y, r->pos.y);
            float vx = r->vel.x - ship->vel.x;
            float vy = r->vel.y - ship->vel.y;
            float v2 = vx * vx + vy * vy;
            if (v2 < 1e-8f) continue;
            float R = 0.04f + r->radius;
            float b = 2.f * (rx * vx + ry * vy);
            float c = rx * rx + ry * ry - R * R;
            float disc = b * b - 4.f * v2 * c;
            if (disc < 0.f) continue;
            float sq = sqrtf(disc);
            float t_hit = (-b - sq) / (2.f * v2);
            if (t_hit <= 0.f || t_hit > 3.f) continue;
            /* Weight by 1/TTC. */
            float w = 1.f / (t_hit + 0.1f);
            float d = sqrtf(rx * rx + ry * ry);
            if (d < 1e-5f) continue;
            away_sum.x -= (rx / d) * w;
            away_sum.y -= (ry / d) * w;
        }
        float as_len = sqrtf(away_sum.x * away_sum.x
                             + away_sum.y * away_sum.y);
        if (as_len > 1e-4f) {
            target_heading = atan2f(away_sum.y, away_sum.x);
        } else {
            /* Fallback: perpendicular to closest threat's velocity. */
            V2 rel = { ship->pos.x - threat_pos.x,
                       ship->pos.y - threat_pos.y };
            rel.x = neo_wrap_delta(0.f, rel.x);
            rel.y = neo_wrap_delta(0.f, rel.y);
            float rel_len = sqrtf(rel.x * rel.x + rel.y * rel.y);
            if (rel_len < 1e-5f) {
                V2 p = { -threat_vel.y, threat_vel.x };
                float plen = sqrtf(p.x * p.x + p.y * p.y);
                if (plen > 1e-5f) { p.x /= plen; p.y /= plen; }
                target_heading = atan2f(p.y, p.x);
            } else {
                V2 perp_a = { -threat_vel.y, threat_vel.x };
                V2 perp_b = {  threat_vel.y, -threat_vel.x };
                float da = perp_a.x * rel.x + perp_a.y * rel.y;
                float db = perp_b.x * rel.x + perp_b.y * rel.y;
                V2 perp = (da > db) ? perp_a : perp_b;
                float plen = sqrtf(perp.x * perp.x + perp.y * perp.y);
                if (plen > 1e-5f) { perp.x /= plen; perp.y /= plen; }
                target_heading = atan2f(perp.y, perp.x);
            }
        }
        target_thrust = panic ? 0.85f : 0.55f;
    } else {
        /* No immediate threat. Drift toward the most open
         * quadrant to keep the field visually alive. */
        /* Sum all rock velocities and head away from the centroid
         * of "where rocks are coming from" — i.e. toward open
         * space. */
        V2 open_dir = {0, 0};
        for (int i = 0; i < MAX_ROCKS; i++) {
            Rock *r = &st->rocks[i];
            if (!r->alive) continue;
            V2 away = { ship->pos.x - r->pos.x,
                        ship->pos.y - r->pos.y };
            away.x = neo_wrap_delta(0.f, away.x);
            away.y = neo_wrap_delta(0.f, away.y);
            float al = sqrtf(away.x * away.x + away.y * away.y);
            if (al > 1e-5f) {
                away.x /= al; away.y /= al;
                /* Weight by inverse distance. */
                float w = 1.f / (al + 0.1f);
                open_dir.x += away.x * w;
                open_dir.y += away.y * w;
            }
        }
        float olen = sqrtf(open_dir.x * open_dir.x + open_dir.y * open_dir.y);
        if (olen > 1e-5f) {
            target_heading = atan2f(open_dir.y, open_dir.x);
        }
        target_thrust = 0.4f;
    }

    /* --- 4. Apply aggression. Higher aggression = sharper turns,
     * fuller thrust, faster trigger. Lower aggression = conservative. */
    float agg = st->aggression;
    target_thrust *= neo_clamp(agg, 0.5f, 1.4f);

    /* --- 5. Convert target_heading into turn_rate. */
    float diff = neo_angle_diff(ship->heading, target_heading);
    /* Aggression also sharpens the turn. */
    float max_turn = NEO_AI_MAX_TURN_RATE * neo_clamp(agg, 0.7f, 1.4f);
    ship->turn_rate = neo_clamp(diff * 6.f, -max_turn, max_turn);

    /* --- 6. Smoothly approach target thrust. */
    ship->thrust_cmd = neo_clamp(target_thrust, 0.f, 1.f);

    /* --- 7. Trigger fire. The ship has a `want_to_shoot` field
     * which fire_bullet reads in physics_step. We compute that here
     * from want_to_shoot (which already accounts for cooldown,
     * panic, and rock presence). */
    ship->want_to_shoot = want_to_shoot;

    /* Debug log — only when perf log is on AND every 60 frames. */
    static int _dbg_counter;
    static int _dbg_enabled;
    if (!_dbg_enabled) _dbg_enabled = (getenv("NCZ_NEO_SPACEWAR_AI_LOG") != NULL);
    if (_dbg_enabled && (_dbg_counter++ % 60) == 0) {
        fprintf(stderr,
            "[diag] neonspacewar ai threat=%d ttc=%.2f shoot=%d "
            "heading=%.2f->%.2f thrust=%.2f\n",
            threat_idx, threat_ttc, shoot_idx,
            ship->heading, target_heading, ship->thrust_cmd);
        fflush(stderr);
    }
}

#endif /* NCZ_NEO_SPACEWAR_AI_H */
