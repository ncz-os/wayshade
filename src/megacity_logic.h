/* SPDX-License-Identifier: Apache-2.0 */
/* megacity_logic.h - the stateless, testable part of megacity.
 *
 * Everything the picture depends on is a pure function of (seed, t) where t is
 * scene seconds: the sun elevation, the weather regime, the lightning flashes,
 * the camera and the ship lanes. Nothing is integrated frame to frame, so a
 * capture at t = 700 s is identical to running for 700 s, and the unit test can
 * check the safety limits over hours of simulated time in milliseconds.
 */
#ifndef MEGACITY_LOGIC_H
#define MEGACITY_LOGIC_H
#include <math.h>
#include <stdint.h>

/* ---- hashing ------------------------------------------------------------ */
static inline uint32_t mc_hash(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA6Bu ^ (c + 0x165667B1u) * 0xC2B2AE35u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
static inline float mc_u(uint32_t h) { return (float)(h & 0xffffffu) / 16777216.f; }
static inline float mc_clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float mc_mix(float a, float b, float t) { return a + (b - a) * t; }
/* Quintic smootherstep: zero first and second derivative at both ends. */
static inline float mc_quintic(float x) {
    x = mc_clamp(x, 0.f, 1.f);
    return x * x * x * (x * (x * 6.f - 15.f) + 10.f);
}

/* ---- sun: one continuous elevation parameter --------------------------- */
/* e = -1 is full night, e = +1 is full pale-reach (bright).
 * cycle: 0 off, 1 dawn (night -> pale-reach once), 2 dusk (the reverse), 3 day (repeats). */
#define MC_TRANSITION_MIN 120.f /* a change of light never takes less than two minutes */
typedef struct {
    int   cycle;
    int   start_luminox;       /* cycle off / day: which state it begins in (legacy name; =1 means pale-reach) */
    float transition;          /* seconds for sunrise or sunset */
    float night_hold, day_hold;/* seconds held at each end */
} mc_cycle;

static inline float mc_sun_raw(const mc_cycle *c, double t);
static inline float mc_sun(const mc_cycle *c, double t) { return mc_clamp(mc_sun_raw(c, t), -1.f, 1.f); }
static inline float mc_sun_raw(const mc_cycle *c, double t) {
    float tr = c->transition < MC_TRANSITION_MIN ? MC_TRANSITION_MIN : c->transition;
    float nh = c->night_hold < 0 ? 0 : c->night_hold, dh = c->day_hold < 0 ? 0 : c->day_hold;
    if (t < 0) t = 0;
    switch (c->cycle) {
    case 1: {
        float u = (float)((t - nh) / tr);
        return -1.f + 2.f * mc_quintic(u);
    }
    case 2: {
        float u = (float)((t - dh) / tr);
        return 1.f - 2.f * mc_quintic(u);
    }
    case 3: {
        double P = 2.0 * tr + nh + dh;
        double p = fmod(t + (c->start_luminox ? (nh + tr) : 0.0), P);
        if (p < nh) return -1.f;
        p -= nh;
        if (p < tr) return -1.f + 2.f * mc_quintic((float)(p / tr));
        p -= tr;
        if (p < dh) return 1.f;
        p -= dh;
        return 1.f - 2.f * mc_quintic((float)(p / tr));
    }
    default:
        return c->start_luminox ? 1.f : -1.f;
    }
}

/* ---- weather ------------------------------------------------------------ */
enum { MC_CLEAR, MC_RAIN, MC_STORM, MC_SNOW, MC_SLEET, MC_FOG, MC_SMOG, MC_DUST, MC_ASH, MC_NWX };
static const char *const MC_WX_NAMES = "clear,rain,storm,snow,sleet,fog,smog,dust,ash";

typedef struct {
    float rain, snow, sleet, fog;   /* fall rates and extra fog density (0..1) */
    float smog, dust, ash, cloud;   /* tint / particles / overcast */
    float wet, wind, storm, pad;    /* surface wetness target, wind, lightning eligibility */
} mc_wx;

static const mc_wx MC_WX_BASE[MC_NWX] = {
    /* rain snow sleet fog  smog dust ash cloud wet  wind storm */
    {0.0f, 0.0f, 0.0f, 0.00f, 0.0f, 0.0f, 0.0f, 0.15f, 0.0f, 0.10f, 0.0f, 0},  /* clear */
    {0.7f, 0.0f, 0.0f, 0.25f, 0.0f, 0.0f, 0.0f, 0.75f, 1.0f, 0.30f, 0.0f, 0},  /* rain */
    {1.0f, 0.0f, 0.0f, 0.30f, 0.0f, 0.0f, 0.0f, 1.00f, 1.0f, 0.70f, 1.0f, 0},  /* storm */
    {0.0f, 0.8f, 0.0f, 0.35f, 0.0f, 0.0f, 0.0f, 0.70f, 0.25f, 0.20f, 0.0f, 0}, /* snow */
    {0.25f, 0.25f, 0.8f, 0.30f, 0.0f, 0.0f, 0.0f, 0.85f, 0.8f, 0.35f, 0.0f, 0},/* sleet */
    {0.0f, 0.0f, 0.0f, 1.00f, 0.0f, 0.0f, 0.0f, 0.60f, 0.3f, 0.05f, 0.0f, 0},  /* fog */
    {0.0f, 0.0f, 0.0f, 0.60f, 1.0f, 0.0f, 0.0f, 0.50f, 0.1f, 0.05f, 0.0f, 0},  /* smog */
    {0.0f, 0.0f, 0.0f, 0.35f, 0.0f, 1.0f, 0.0f, 0.30f, 0.0f, 0.80f, 0.0f, 0},  /* dust */
    {0.0f, 0.0f, 0.0f, 0.45f, 0.0f, 0.0f, 1.0f, 0.80f, 0.0f, 0.30f, 0.0f, 0},  /* ash */
};
static const float MC_WX_WEIGHT[MC_NWX] = {2.0f, 2.0f, 1.0f, 1.5f, 0.8f, 1.2f, 1.0f, 0.7f, 0.5f};

typedef struct {
    uint32_t seed;
    unsigned allowed;     /* bit mask of MC_* types the regimes may use */
    float    rmin, rmax;  /* regime duration range, seconds (3..8 minutes by default) */
    float    intensity;   /* scales the fall rates and densities */
} mc_wx_cfg;

static inline float mc_regime_len(const mc_wx_cfg *w, uint32_t k) {
    return w->rmin + (w->rmax - w->rmin) * mc_u(mc_hash(w->seed, k, 1));
}
/* Type of regime k: weighted by MC_WX_WEIGHT among allowed types, never the same twice in a row. */
static inline int mc_regime_type(const mc_wx_cfg *w, uint32_t k) {
    int prev = -1, cur = 0;
    for (uint32_t i = 0; i <= k; i++) {
        float tot = 0;
        for (int j = 0; j < MC_NWX; j++)
            if ((w->allowed >> j & 1u) && j != prev) tot += MC_WX_WEIGHT[j];
        if (tot <= 0) { for (int j = 0; j < MC_NWX; j++) if (w->allowed >> j & 1u) { cur = j; break; } prev = cur; continue; }
        float r = mc_u(mc_hash(w->seed, i, 2)) * tot, acc = 0;
        cur = 0;
        for (int j = 0; j < MC_NWX; j++) {
            if (!(w->allowed >> j & 1u) || j == prev) continue;
            acc += MC_WX_WEIGHT[j];
            cur = j;
            if (r < acc) break;
        }
        prev = cur;
    }
    return cur;
}
static inline int mc_single(const mc_wx_cfg *w) { return __builtin_popcount(w->allowed) <= 1; }

/* Weather at time t: the previous regime's parameters blend into the current
 * one's over a quintic window at the start of each regime. Wetness dries at
 * half the speed it soaks. */
static inline mc_wx mc_weather(const mc_wx_cfg *w, double t, int *cur_type, float *blend) {
    mc_wx out;
    int type;
    if (mc_single(w)) {
        type = 0;
        for (int j = 0; j < MC_NWX; j++) if (w->allowed >> j & 1u) { type = j; break; }
        out = MC_WX_BASE[type];
        if (cur_type) *cur_type = type;
        if (blend) *blend = 1.f;
    } else {
        uint32_t k = 0;
        double T = 0;
        for (;;) {
            double d = mc_regime_len(w, k);
            if (t < T + d) break;
            T += d;
            k++;
        }
        double d = mc_regime_len(w, k);
        float tw = mc_clamp((float)(0.25 * d), 45.f, 90.f);
        float u = (float)((t - T) / tw);
        type = mc_regime_type(w, k);
        int ptype = k ? mc_regime_type(w, k - 1) : type;
        float b = k ? mc_quintic(u) : 1.f;
        const float *A = (const float *)&MC_WX_BASE[ptype], *B = (const float *)&MC_WX_BASE[type];
        float *O = (float *)&out;
        for (int i = 0; i < 12; i++) O[i] = A[i] + (B[i] - A[i]) * b;
        if (k && MC_WX_BASE[type].wet < MC_WX_BASE[ptype].wet) {
            float bw = mc_quintic(u * 0.5f);
            out.wet = MC_WX_BASE[ptype].wet + (MC_WX_BASE[type].wet - MC_WX_BASE[ptype].wet) * bw;
        }
        if (cur_type) *cur_type = type;
        if (blend) *blend = b;
    }
    float in = w->intensity;
    out.rain *= in; out.snow *= in; out.sleet *= in; out.fog *= in;
    out.smog *= in; out.dust *= in; out.ash *= in;
    return out;
}

/* ---- lightning ---------------------------------------------------------- */
/* Hard limits, checked by tests/test_megacity.c over hours of simulated time:
 *   at most 3 flashes per second (peaks are at least 0.40 s apart, so at most 2.5 per second),
 *   a soft rise of 100 to 300 ms (quintic, so no step in level or slope),
 *   a peak luminance delta of at most `amp` (default 0.05, ceiling 0.20). */
#define MC_FLASH_DEFAULT 0.05f
#define MC_FLASH_CEILING 0.20f
#define MC_FLASH_BUCKET  10.0
#define MC_FLASH_MIN_GAP 0.40f

typedef struct { double t0; float rise, tau, amp; } mc_flash_ev;

/* Events starting in bucket b (up to 3 in a cluster). Returns the count. */
static inline int mc_flash_events(uint32_t seed, uint32_t b, float amp, mc_flash_ev *ev) {
    if (mc_u(mc_hash(seed, b, 11)) >= 0.62f) return 0;
    double s0 = b * MC_FLASH_BUCKET + mc_u(mc_hash(seed, b, 12)) * (MC_FLASH_BUCKET - 3.6);
    int n = 1 + (mc_u(mc_hash(seed, b, 13)) > 0.55f) + (mc_u(mc_hash(seed, b, 14)) > 0.85f);
    /* onsets are spaced so that the PEAKS (onset + rise, the rise varying by 0.2 s) stay >= 0.4 s apart */
    float gap = MC_FLASH_MIN_GAP + 0.20f + 0.02f + 0.4f * mc_u(mc_hash(seed, b, 15));
    for (int i = 0; i < n; i++) {
        ev[i].t0 = s0 + i * gap;
        ev[i].rise = 0.10f + 0.20f * mc_u(mc_hash(seed, b, 20 + i));
        ev[i].tau = 0.25f + 0.35f * mc_u(mc_hash(seed, b, 30 + i));
        ev[i].amp = amp * (0.65f + 0.35f * mc_u(mc_hash(seed, b, 40 + i)));
    }
    return n;
}
static inline float mc_flash_env(const mc_flash_ev *e, double t) {
    double x = t - e->t0;
    if (x <= 0) return 0.f;
    if (x < e->rise) return e->amp * mc_quintic((float)(x / e->rise));
    return e->amp * expf(-(float)(x - e->rise) / e->tau);
}
/* Flash level at t (0..amp), scaled by how much of a storm the weather is. */
static inline float mc_flash(uint32_t seed, double t, float amp, float storm) {
    if (amp <= 0 || storm <= 0.001f) return 0.f;
    if (amp > MC_FLASH_CEILING) amp = MC_FLASH_CEILING;
    long b = (long)floor(t / MC_FLASH_BUCKET);
    float m = 0;
    for (long k = b - 1; k <= b; k++) {
        if (k < 0) continue;
        mc_flash_ev ev[3];
        int n = mc_flash_events(seed, (uint32_t)k, amp, ev);
        for (int i = 0; i < n; i++) { float v = mc_flash_env(&ev[i], t); if (v > m) m = v; }
    }
    /* storm is a quintic-blended weight, so flashes fade in with the weather */
    return m * mc_quintic(storm);
}
/* Azimuth (radians) of the strike in bucket b, for the cloud glow and the bolt. */
static inline float mc_flash_az(uint32_t seed, double t) {
    long b = (long)floor(t / MC_FLASH_BUCKET);
    double s = t - b * MC_FLASH_BUCKET;
    /* the tail of the previous bucket's strike belongs to that bucket's azimuth */
    long k = (s < 3.6 && b > 0) ? b - 1 : b;
    if (k < 0) k = 0;
    return 6.2831853f * mc_u(mc_hash(seed, (uint32_t)k, 50));
}

/* ---- camera ------------------------------------------------------------- */
#define MC_CELL 64.0     /* metres: one fine lattice cell */
#define MC_BLOCK 8       /* cells per block; the rim cells are the boulevards */

typedef struct {
    double x, y, z;      /* metres, world */
    float  yaw, pitch, roll;
} mc_cam;

static inline mc_cam mc_camera(uint32_t seed, double t, float speed, float alt, float sway) {
    float p1 = 6.2831853f * mc_u(mc_hash(seed, 101, 0)), p2 = 6.2831853f * mc_u(mc_hash(seed, 102, 0));
    float p3 = 6.2831853f * mc_u(mc_hash(seed, 103, 0)), p4 = 6.2831853f * mc_u(mc_hash(seed, 104, 0));
    mc_cam c;
    c.z = t * 34.0 * speed;
    c.x = sway * (20.0 * sin(0.047 * t + p1) + 7.0 * sin(0.113 * t + p2));
    double y = 190.0 + 90.0 * sin(0.031 * t + p3) + 35.0 * sin(0.083 * t + p4);
    y *= alt;
    if (y < 70.0) y = 70.0;
    c.y = y;
    c.yaw = 0.36f * sinf(0.019f * (float)t + p2) + 0.12f * sinf(0.051f * (float)t + p3);
    c.pitch = -0.05f + 0.07f * sinf(0.023f * (float)t + p4);
    float dyaw = 0.36f * 0.019f * cosf(0.019f * (float)t + p2) + 0.12f * 0.051f * cosf(0.051f * (float)t + p3);
    c.roll = -1.6f * dyaw;
    return c;
}

/* ---- ships and traffic --------------------------------------------------- */
#define MC_NSHIP 5     /* slots 0..3 mid-size freighters, slot 4 the rare colossal capital */
#define MC_NCRAFT 24

typedef struct {
    double p[3];       /* world position, metres */
    float  yaw;        /* heading about +y */
    float  scale;      /* hull length, metres */
    uint32_t id;       /* hull seed */
    int    kind;       /* hull family 0..4 */
    float  fade;       /* 0..1 at the ends of the crossing so nothing pops */
    int    live;
} mc_ship;

/* The slot's occupant n is built from (seed, slot, n) alone. It crosses the
 * view once: at the middle of its life it is `f` metres ahead of where the
 * camera is then, `l` to one side and `a` above the street. */
typedef struct { double cam_x, cam_y, cam_z; } mc_anchor;

static inline mc_ship mc_ship_slot(uint32_t seed, int slot, double t, mc_anchor (*camat)(void *, double), void *ctx,
                                   float density, float capital_rate) {
    mc_ship s;
    s.live = 0; s.fade = 0; s.scale = 1; s.kind = 0; s.id = 0; s.yaw = 0;
    s.p[0] = s.p[1] = s.p[2] = 0;
    int cap = slot == MC_NSHIP - 1;
    double P = cap ? 420.0 : 70.0 + 26.0 * slot;   /* occupant lifetime, seconds */
    double phi = P * mc_u(mc_hash(seed, 200 + slot, 0));
    double tt = t + phi;
    long n = (long)floor(tt / P);
    double age = tt - n * P;
    uint32_t h = mc_hash(seed, 300 + slot, (uint32_t)n);
    if (cap) {
        if (mc_u(mc_hash(h, 1, 0)) >= capital_rate) return s;
    } else {
        int nslots = (int)ceilf(density * 3.0f);
        if (nslots > 4) nslots = 4;
        if (slot >= nslots) return s;
    }
    double tm = t - age + 0.5 * P;                 /* mid-life time */
    mc_anchor a = camat(ctx, tm);
    float u1 = mc_u(mc_hash(h, 2, 0)), u2 = mc_u(mc_hash(h, 3, 0)), u3 = mc_u(mc_hash(h, 4, 0));
    float u4 = mc_u(mc_hash(h, 5, 0)), u5 = mc_u(mc_hash(h, 6, 0));
    float len, alt, fwd, lat, spd;
    if (cap) {
        len = 2400.f + 1400.f * u1;               /* 2.4 to 3.8 km */
        alt = 1350.f + 500.f * u2;
        fwd = 5200.f + 1800.f * u3;
        lat = (u4 < 0.5f ? -1.f : 1.f) * (500.f + 1300.f * u5);
        spd = 16.f + 10.f * u1;
    } else {
        len = 110.f + 220.f * u1 * u1;             /* 110 to 330 m */
        alt = 480.f + 520.f * u2;
        fwd = 1300.f + 2300.f * u3;
        lat = (u4 < 0.5f ? -1.f : 1.f) * (120.f + 1100.f * u5);
        spd = 38.f + 55.f * u1;
    }
    /* heading: mostly across the view, sometimes along it */
    float mode = mc_u(mc_hash(h, 7, 0));
    float dir_x, dir_z;
    if (mode < 0.62f) { dir_x = (lat < 0 ? 1.f : -1.f) * (0.8f + 0.2f * u3); dir_z = (u5 - 0.5f) * 0.4f; }
    else if (mode < 0.85f) { dir_x = (u4 - 0.5f) * 0.3f; dir_z = 1.f; }
    else { dir_x = (u4 - 0.5f) * 0.3f; dir_z = -1.f; }
    float nn = sqrtf(dir_x * dir_x + dir_z * dir_z);
    dir_x /= nn; dir_z /= nn;
    double dt = t - tm;
    s.p[0] = a.cam_x + lat + dir_x * spd * dt;
    s.p[1] = alt;
    s.p[2] = a.cam_z + fwd + dir_z * spd * dt;
    s.yaw = atan2f(dir_x, dir_z);
    s.scale = len;
    s.id = h;
    s.kind = cap ? 4 : (int)(mc_u(mc_hash(h, 8, 0)) * 4.f) % 4;
    float e = (float)(age / P);
    s.fade = mc_quintic(e / 0.12f) * mc_quintic((1.f - e) / 0.12f);
    s.live = s.fade > 0.f;
    return s;
}

/* Small lane traffic: NCRAFT craft on three altitude tiers running along the
 * boulevards. Craft j sits at a fixed phase on a ring of length RING that
 * slides past the camera, so the same craft are always where the seed says. */
#define MC_RING 3200.0
typedef struct { double x, y, z; float dir, fade; uint32_t id; } mc_craft;

static inline mc_craft mc_craft_at(uint32_t seed, int j, double t, double cam_x, double cam_z, float traffic) {
    (void)cam_x;
    mc_craft c;
    uint32_t h = mc_hash(seed, 400 + (uint32_t)j, 0);
    float tier = mc_u(mc_hash(h, 1, 0));
    c.y = tier < 0.34f ? 55.0 + 25.0 * mc_u(mc_hash(h, 2, 0)) : tier < 0.7f ? 130.0 + 40.0 * mc_u(mc_hash(h, 2, 0))
                                                                          : 250.0 + 60.0 * mc_u(mc_hash(h, 2, 0));
    int blv = (int)(mc_u(mc_hash(h, 3, 0)) * 3.f) - 1;               /* boulevard -1, 0, 1 */
    float side = mc_u(mc_hash(h, 4, 0)) < 0.5f ? -1.f : 1.f;
    c.x = blv * 512.0 + side * (42.0 + 16.0 * mc_u(mc_hash(h, 5, 0)));
    c.dir = side;                                                    /* right-hand lanes: +x side runs +z */
    double v = (28.0 + 32.0 * mc_u(mc_hash(h, 6, 0))) * side;
    double base = MC_RING * mc_u(mc_hash(h, 7, 0));
    double zr = fmod(base + v * t - cam_z, MC_RING);
    if (zr < 0) zr += MC_RING;
    zr -= 0.25 * MC_RING;                                            /* 800 m behind to 2400 m ahead */
    c.z = cam_z + zr;
    float e = (float)((zr + 0.25 * MC_RING) / MC_RING);
    c.fade = mc_quintic(e / 0.06f) * mc_quintic((1.f - e) / 0.06f);
    c.id = h;
    /* traffic scales how many of the 24 are live */
    float rank = (float)j / (float)MC_NCRAFT;
    if (rank >= traffic * 0.5f) c.fade = 0.f;
    return c;
}
#endif
