/* SPDX-License-Identifier: Apache-2.0 */
/* blackhole_scenes.h - per-frame state of the Black Hole scenes.
 *
 * Scenes are heavier render modes of blackhole_gles3 selected with --scene.
 * The shader is specialized at compile time (BH_SCENE, BH_LIGHT), so the
 * default single-hole scene pays nothing for them. This file holds only the
 * CPU side: the orbital mechanics and timelines that place the holes, the
 * star of a tidal disruption and the camera of the galaxy zoom each frame.
 *
 * Accuracy: every scene is ARTISTIC. The orbits follow the right laws
 * (Kepler, the quadrupole inspiral a ~ (t_c - t)^(1/4), Barker's equation for
 * a parabolic orbit, the figure-eight three-body choreography), but the time
 * and length scales are compressed for viewing, and the lensing of several
 * holes is a superposition, not a solution of the field equations.
 */
#ifndef BLACKHOLE_SCENES_H
#define BLACKHOLE_SCENES_H
#include <math.h>
#include <stdint.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BH_SCENES "single,binary,merger,tidal-disruption,galaxy-zoom,three-body"
enum { BH_SC_SINGLE, BH_SC_BINARY, BH_SC_MERGER, BH_SC_TDE, BH_SC_GALAXY, BH_SC_THREE, BH_SC_COUNT };

static int bh_scene_id(const char *n) {
    static const char *t[] = {"single", "binary", "merger", "tidal-disruption", "galaxy-zoom", "three-body"};
    for (int i = 0; i < BH_SC_COUNT; i++)
        if (!strcmp(n, t[i])) return i;
    return 0;
}

/* Default cycle length (s) of the scenes that restart; 0 = continuous. */
static const double BH_SC_CYCLE[BH_SC_COUNT] = {0, 0, 75, 110, 80, 0};
/* Default camera path (flyby type) when --flyby=auto, and distance range. */
static const int BH_SC_FLY[BH_SC_COUNT] = {0, 1, 2, 2, 2, 7};
static const double BH_SC_DMIN[BH_SC_COUNT] = {0, 26, 28, 20, 14, 24};
static const double BH_SC_DMAX[BH_SC_COUNT] = {0, 34, 36, 27, 20, 32};
/* Field-of-view zoom (telephoto) and default orbit-plane inclination (radians,
 * used when --inclination is automatic) per scene. */
static const float BH_SC_ZOOM[BH_SC_COUNT] = {1, 1.35f, 1.35f, 1, 1, 1.3f};
static const float BH_SC_INCL[BH_SC_COUNT] = {-1, 0.5f, 0.5f, 0.6f, 0.72f, 0.45f};

typedef struct {
    int scene;      /* BH_SC_* */
    int light;      /* 1 = light (thin-lens) mode for weak GPUs */
    double cycle;   /* seconds per cycle for restarting scenes */
    float q;        /* mass ratio option, 0 = per launch/cycle random */
    float sep;      /* separation option, 0 = default */
    float ripples;  /* gravitational-wave ripple strength */
    uint32_t seed;
} BhSceneCfg;

/* Everything the shader needs for one frame. */
typedef struct {
    float holes[12];  /* 3 x (x, y, z, r_s); r_s 0 = absent */
    float scn[4];     /* minidisk outer radius (own r_s), circumbinary inner, outer, brightness */
    float gw[4];      /* ripple amplitude, orbital omega0, t_c, phase0 */
    float gw2[4];     /* scene time, merge time (<0 none), wave speed, ringdown omega */
    float fx[4];      /* merger flash, ringdown wobble, minidisk visibility, remnant r_s */
    float tde[4];     /* star position xyz, star radius (0 = gone) */
    float tde2[4];    /* time since pericenter, r_p, pericenter angle, orbit sense */
    float tde3[4];    /* tidal stretch, flare, disk outer radius, disk brightness */
    float gal[4];     /* arms, pitch (tan), disk scale length, rotation phase */
    float gal2[4];    /* hue, dust, bulge sigma, cycle variation */
    float zoom[4];    /* camera distance (0 = path), star density scale, nebula scale, galaxy brightness */
    float fade;       /* cycle fade 0..1 */
} BhSceneFrame;

static uint32_t bhs_hash(uint32_t a, uint32_t b) {
    uint32_t h = a * 2654435761u ^ (b + 0x9E3779B9u);
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}
/* uniform 0..1 from (seed, cycle, slot) */
static float bhs_u(uint32_t seed, long k, int slot) {
    return (float)(bhs_hash(bhs_hash(seed, (uint32_t)k), (uint32_t)slot) >> 8) / 16777216.f;
}
static double bhs_sstep(double a, double b, double x) {
    double t = (x - a) / (b - a);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
}

/* Two holes on a circular orbit around the barycenter (origin), orbital plane z = 0. */
static void bhs_pair(BhSceneFrame *f, float m1, float m2, float a, float phi) {
    float M = m1 + m2, c = cosf(phi), s = sinf(phi);
    float r1 = a * m2 / M, r2 = a * m1 / M;
    float h[12] = {-r1 * c, -r1 * s, 0, m1, r2 * c, r2 * s, 0, m2, 0, 0, 0, 0};
    memcpy(f->holes, h, sizeof h);
}

/* Fill f for scene time t (seconds, already scaled by --speed). */
static void bh_scene_frame(const BhSceneCfg *c, double t, BhSceneFrame *f) {
    memset(f, 0, sizeof *f);
    f->fade = 1.f;
    f->fx[2] = 1.f;
    f->zoom[1] = 1.f;
    f->zoom[2] = 1.f;
    f->gw2[0] = (float)t;
    f->gw2[1] = -1.f;
    f->gw2[2] = 10.f;
    long k = 0;
    double ts = t, T = c->cycle;
    if (T > 0) {
        k = (long)floor(t / T);
        ts = t - (double)k * T;
        f->fade = (float)(bhs_sstep(0, 1.5, ts) * bhs_sstep(0, 2.0, T - ts));
        f->gw2[0] = (float)ts;
    }
    uint32_t sd = c->seed;
    switch (c->scene) {
    case BH_SC_BINARY: {
        /* Kepler-scaled circular binary. Period ~30 s at a = 15 r_s: calm, not spinny. */
        float q = c->q > 0 ? c->q : 0.45f + 0.55f * bhs_u(sd, 0, 1);
        float a = c->sep > 0 ? c->sep : 14.f + 3.f * bhs_u(sd, 0, 2);
        double P = 30.0 * pow(a / 15.0, 1.5);
        float w0 = (float)(2 * M_PI / P), ph0 = 6.2831853f * bhs_u(sd, 0, 3);
        float phi = ph0 + (float)fmod(w0 * t, 2 * M_PI);
        bhs_pair(f, 1.f, q, a, phi);
        f->scn[0] = 0.40f * a;             /* minidisk outer radius, own r_s units */
        f->scn[1] = 1.6f * a;              /* circumbinary ring */
        f->scn[2] = 2.2f * a;
        f->scn[3] = 0.45f;
        f->gw[0] = 0.35f * c->ripples;
        f->gw[1] = w0;
        f->gw[2] = 1e9f;
        f->gw[3] = phi;                    /* shader uses phase(t_ret) = gw3 + w0 (t_ret - t) */
        f->gw2[0] = 0;                     /* binary: time relative to now */
        break;
    }
    case BH_SC_MERGER: {
        /* Quadrupole inspiral: a = a0 (1 - t/t_c)^(1/4), omega ~ a^(-3/2),
         * phase = phi0 + omega0 (8/5) t_c (1 - (1 - t/t_c)^(5/8)). The holes
         * merge at a_min (about when the two shadows touch); over 0.8 s the two
         * shadows slide together into one (the lighter one shrinking into the
         * remnant), the remnant's shadow rings down (decaying rotating
         * quadrupole), and after a delay the circumbinary disk falls in and
         * brightens as it feeds the remnant. A vacuum merger emits no light,
         * so there is no flash. */
        float q = c->q > 0 ? c->q : 0.5f + 0.5f * bhs_u(sd, k, 1);
        float m1 = 1.f, m2 = q, M = m1 + m2;
        float a0 = c->sep > 0 ? c->sep : 16.f;
        float amin = 2.1f * M;
        if (a0 < amin * 1.5f) a0 = amin * 1.5f;
        double tm = 0.70 * T;
        double tc = tm / (1.0 - pow(amin / a0, 4.0));
        double P0 = 24.0 * pow(a0 / 16.0, 1.5);
        float w0 = (float)(2 * M_PI / P0), ph0 = 6.2831853f * bhs_u(sd, k, 3);
        float rsf = 0.95f * M;                            /* ~5% radiated */
        f->gw[0] = c->ripples;
        f->gw[1] = w0;
        f->gw[2] = (float)tc;
        f->gw[3] = ph0;
        f->gw2[1] = (float)tm;
        f->gw2[2] = 9.f;
        f->gw2[3] = (float)(2 * M_PI / 0.9);              /* ringdown, slowed for viewing */
        float cbin0 = 1.8f * fmaxf(0.55f * a0, amin);
        if (ts < tm) {
            double x = 1.0 - ts / tc;
            float a = (float)(a0 * pow(x, 0.25));
            float phi = (float)(ph0 + w0 * 1.6 * tc * (1.0 - pow(x, 0.625)));
            bhs_pair(f, m1, m2, a, phi);
            f->scn[0] = fmaxf(0.40f * a, 3.4f);
            f->fx[2] = (float)(1.0 - bhs_sstep(tm - 2.0, tm, ts));
            f->scn[1] = 1.8f * fmaxf(a, 0.55f * a0);
            f->scn[3] = 0.40f;
        } else {
            double tr = ts - tm;
            double xm = 1.0 - tm / tc;
            float phim = (float)(ph0 + w0 * 1.6 * tc * (1.0 - pow(xm, 0.625)));
            float wm = (float)(w0 * pow(xm, -0.375));      /* orbital rate at the merger */
            float g = (float)bhs_sstep(0, 0.8, tr);         /* coalescence of the two shadows */
            bhs_pair(f, m1, m2, amin * (1.f - g), phim + wm * (float)fmin(tr, 0.8) * (1.f - 0.5f * g));
            f->holes[3] = m1 + (rsf - m1) * g;
            f->holes[7] = m2 * (1.f - g);
            f->fx[1] = (float)(0.34 * g * exp(-tr / 3.2));
            f->fx[2] = 0.f;
            float e = (float)bhs_sstep(1.5, 12, tr);
            f->scn[1] = cbin0 + (3.1f * rsf - cbin0) * e;
            f->scn[3] = 0.40f + 0.55f * e;
        }
        f->fx[3] = rsf;
        f->scn[2] = f->scn[1] + 0.75f * a0;
        break;
    }
    case BH_SC_THREE: {
        /* Figure-eight choreography (Moore 1993; Chenciner and Montgomery 2000):
         * three equal masses chase each other along one figure eight, phases
         * 0, T/3, 2T/3. x = S sin(tau), y = 0.315 S sin(2 tau) is a close fit
         * to the exact curve; the figure slowly turns so the view changes. */
        float S = c->sep > 0 ? c->sep : 11.f;
        float m = 0.8f;
        double P = 48.0 * pow(S / 11.0, 1.5);
        double base = 2 * M_PI * fmod(t / P, 1.0);
        float rot = 6.2831853f * bhs_u(sd, 0, 4) + (float)fmod(t * 2 * M_PI / 420.0, 2 * M_PI);
        float cr = cosf(rot), sr = sinf(rot);
        for (int i = 0; i < 3; i++) {
            double tau = base + 2 * M_PI * i / 3.0;
            float x = S * (float)sin(tau), y = 0.315f * S * (float)sin(2 * tau);
            f->holes[4 * i + 0] = cr * x - sr * y;
            f->holes[4 * i + 1] = sr * x + cr * y;
            f->holes[4 * i + 2] = 0;
            f->holes[4 * i + 3] = m;
        }
        f->scn[0] = 5.2f;
        break;
    }
    case BH_SC_TDE: {
        /* A star on a parabolic orbit (Barker's equation), pericenter r_p,
         * torn apart there: the star stretches, sheds into a stream whose bound
         * half wraps around the hole and circularizes into a new, flaring
         * disk (fallback rate ~ t^(-5/3)), the unbound half escapes. */
        float rp = 5.5f;
        double tp = 0.12 * T;                      /* pericenter passage (early: no dead time) */
        float om = 6.2831853f * bhs_u(sd, k, 5);
        float sg = bhs_u(sd, k, 6) < 0.5f ? -1.f : 1.f;
        double W = (ts - tp) * 0.6, w = 1.5 * W, s = sqrt(w * w + 1.0);
        double D = cbrt(w + s) - cbrt(s - w);      /* D = tan(f/2) */
        double fa = 2.0 * atan(D), r = rp * (1.0 + D * D);
        f->tde[0] = (float)(r * cos(om + sg * fa));
        f->tde[1] = (float)(r * sin(om + sg * fa));
        f->tde[2] = 0;
        f->tde[3] = (float)(2.0 * (1.0 - bhs_sstep(-0.55, 0.35, D)));
        double tau = ts - tp;
        f->tde2[0] = (float)tau;
        f->tde2[1] = rp;
        f->tde2[2] = om;
        f->tde2[3] = sg;
        f->tde3[0] = (float)(1.0 + 2.5 * pow(rp / r, 3.0));
        double fl = tau < 0 ? 0 : (1.0 - exp(-tau / 3.0)) * (0.35 + 1.6 * pow(1.0 + tau / 8.0, -5.0 / 3.0));
        f->tde3[1] = (float)fl;
        f->tde3[2] = (float)(3.8 + (2.3 * rp - 3.8) * bhs_sstep(3, 45, tau));
        /* a faint quiescent disk before, the new disk after the disruption */
        f->tde3[3] = (float)(0.12 + bhs_sstep(2, 18, tau) * (0.45 + 0.9 * exp(-tau / 28.0)));
        break;
    }
    case BH_SC_GALAXY: {
        /* Powers-of-ten dive: hold on the whole galaxy, a logarithmic zoom to
         * the nucleus, then linger at the hole. Distances in r_s; the galaxy
         * is scaled down enormously (a real one is ~1e10 r_s across). */
        double u = ts / T, D0 = 26000.0, D1 = 15.0, e;
        if (u < 0.07) e = 0;
        else if (u < 0.74) { double x = (u - 0.07) / 0.67; e = x * x * x * (x * (x * 6 - 15) + 10); }
        else e = 1;
        double D = exp(log(D0) + (log(D1) - log(D0)) * e);
        if (u >= 0.74) D *= 1.0 + 0.25 * sin((u - 0.74) / 0.26 * M_PI);
        f->zoom[0] = (float)D;
        double nearv = bhs_sstep(log(4000.0), log(60.0), log(D));
        f->zoom[1] = (float)(0.10 + 0.90 * nearv);
        f->zoom[2] = (float)nearv;
        f->zoom[3] = 1.f;
        f->gal[0] = (float)(2 + (bhs_u(sd, k, 7) < 0.25f ? 1 : 0) + (bhs_u(sd, k, 8) < 0.1f ? 1 : 0));
        f->gal[1] = 0.22f + 0.2f * bhs_u(sd, k, 9);
        f->gal[2] = 3000.f;
        f->gal[3] = 6.2831853f * bhs_u(sd, k, 10) + (float)(ts * 0.004);
        f->gal2[0] = bhs_u(sd, k, 11);
        f->gal2[1] = 0.55f + 0.45f * bhs_u(sd, k, 12);
        f->gal2[2] = 900.f;
        f->gal2[3] = bhs_u(sd, k, 13);
        break;
    }
    default:
        break;
    }
}
#endif
