/* SPDX-License-Identifier: Apache-2.0 */
/* blackhole_director.h - the "director" camera and the analytic Kerr shadow.
 *
 * Director: a seeded, never-cutting sequence of observations. Each chapter is
 * establish (wide and high) -> approach (oblique, closing in) -> reveal (the
 * camera rises out of the disk plane, so the far side of the disk appears
 * over the top) -> dwell (slow drift) -> withdraw. Every shot starts where the
 * previous one ended and eases in and out (smootherstep), so angular speed
 * and acceleration stay bounded. The up vector is the disk normal: no roll.
 * Hold times, heights, azimuth drift and distances are drawn per shot from
 * the launch seed, so chapters do not repeat.
 *
 * Kerr shadow: the Bardeen (1973) shadow outline for spin a and observer
 * inclination theta from the spin axis, tabulated as radius versus position
 * angle for 10 inclinations (0..90 degrees). The ray integrator stays
 * Schwarzschild; only the capture boundary follows the Kerr outline.
 */
#ifndef BLACKHOLE_DIRECTOR_H
#define BLACKHOLE_DIRECTOR_H
#include <math.h>
#include <stdint.h>
#include "blackhole_scenes.h"

typedef struct {
    double near_, far_;          /* distance range of the chapter (r_s) */
    uint32_t seed;
    long shot;                   /* index of the current shot */
    double t_shot, dur;          /* start time and length of the current shot */
    double r0, r1, e0, e1, a0, a1; /* start/end distance, elevation (rad), azimuth (rad) */
    double f0x, f0y, f1x, f1y;   /* start/end framing offset (screen units): the hole is not always centered */
} BhDirector;

static double bhd_ease(double x) {
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    return x * x * x * (x * (x * 6 - 15) + 10);
}

/* Parameters of shot k (5-shot chapters), continuing from (r, e, a). */
static void bhd_plan(BhDirector *d, long k, double r, double e, double a) {
    uint32_t sd = d->seed;
    float u1 = bhs_u(sd, k, 21), u2 = bhs_u(sd, k, 22), u3 = bhs_u(sd, k, 23), u4 = bhs_u(sd, k, 24);
    const double deg = M_PI / 180.0, N = d->near_, F = d->far_;
    double dir = bhs_u(sd, k / 5, 25) < 0.5 ? -1 : 1;    /* drift sense per chapter */
    d->r0 = r; d->e0 = e; d->a0 = a;
    d->f0x = d->f1x; d->f0y = d->f1y;
    /* rule-of-thirds style framing: off center on most shots, centered on some */
    d->f1x = (bhs_u(sd, k, 27) < 0.3f) ? 0.0 : (bhs_u(sd, k, 28) < 0.5f ? -1 : 1) * (0.18 + 0.14 * bhs_u(sd, k, 29));
    d->f1y = (bhs_u(sd, k, 30) - 0.5) * 0.2;
    switch (k % 5) {
    case 0: /* establish: wide and high */
        d->dur = 14 + 10 * u1; d->r1 = F * (0.9 + 0.2 * u2); d->e1 = (35 + 25 * u3) * deg; d->a1 = a + dir * (0.25 + 0.3 * u4); break;
    case 1: /* approach: oblique, closing in, sinking toward the plane */
        d->dur = 35 + 25 * u1; d->r1 = N * (1.05 + 0.35 * u2); d->e1 = (6 + 8 * u3) * deg; d->a1 = a + dir * (0.8 + 0.8 * u4); break;
    case 2: /* reveal: rise out of the plane, the far side of the disk arches over */
        d->dur = 20 + 15 * u1; d->r1 = N * (1.1 + 0.4 * u2); d->e1 = (18 + 16 * u3) * deg; d->a1 = a + dir * (0.3 + 0.4 * u4); break;
    case 3: /* dwell: slow drift at a revealing height */
        d->dur = 30 + 30 * u1; d->r1 = N * (1.2 + 0.6 * u2); d->e1 = (8 + 22 * u3) * deg; d->a1 = a + dir * (0.6 + 0.9 * u4); break;
    default: /* withdraw */
        d->dur = 14 + 10 * u1; d->r1 = F * (0.7 + 0.3 * u2); d->e1 = (20 + 25 * u3) * deg; d->a1 = a + dir * (0.3 + 0.3 * u4); break;
    }
    if (d->e1 < 3 * deg) d->e1 = 3 * deg;                /* never exactly edge-on */
}

static void bhd_init(BhDirector *d, uint32_t seed, double near_, double far_) {
    d->seed = seed; d->near_ = near_; d->far_ = far_;
    d->shot = 0; d->t_shot = 0; d->f1x = d->f1y = 0;
    bhd_plan(d, 0, far_ * 1.1, 50 * M_PI / 180, 6.2831853 * bhs_u(seed, 0, 26));
}

/* Camera position at time t (seconds); t must not decrease. */
static void bhd_camera(BhDirector *d, double t, float out[3], float frame[2]) {
    while (t > d->t_shot + d->dur) {
        d->t_shot += d->dur; d->shot++;
        bhd_plan(d, d->shot, d->r1, d->e1, d->a1);
    }
    double x = bhd_ease((t - d->t_shot) / d->dur);
    double r = exp(log(d->r0) + (log(d->r1) - log(d->r0)) * x);   /* distance eases in log space */
    double e = d->e0 + (d->e1 - d->e0) * x, a = d->a0 + (d->a1 - d->a0) * x;
    out[0] = (float)(r * cos(e) * cos(a));
    out[1] = (float)(r * cos(e) * sin(a));
    out[2] = (float)(r * sin(e));
    frame[0] = (float)(d->f0x + (d->f1x - d->f0x) * x);
    frame[1] = (float)(d->f0y + (d->f1y - d->f0y) * x);
}

/* Kerr shadow table: tab[i*32 + j] = boundary radius (r_s) at inclination
 * i*10 degrees (i = 0..9, 0 clamped to 3 degrees) and position angle
 * j*2pi/32 measured from the alpha axis (perpendicular to the projected spin). */
static void bh_kerr_table(float a, float tab[320]) {
    if (a < 1e-3) a = 1e-3;
    if (a > 0.998f) a = 0.998f;
    double r1 = 2 * (1 + cos(2.0 / 3.0 * acos(-a))), r2 = 2 * (1 + cos(2.0 / 3.0 * acos(a)));
    for (int i = 0; i < 10; i++) {
        double th = (i == 0 ? 3.0 : i * 10.0) * M_PI / 180, st = sin(th), ct = cos(th);
        double px[1600], py[1600]; int n = 0;
        for (int s = 0; s < 800; s++) {
            double r = r1 + (r2 - r1) * (s + 0.5) / 800;
            double xi = (r * r * (3 - r) - a * a * (r + 1)) / (a * (r - 1));
            double eta = r * r * r * (4 * a * a - r * (r - 3) * (r - 3)) / (a * a * (r - 1) * (r - 1));
            double al = -xi / st, b2 = eta + a * a * ct * ct - xi * xi * ct * ct / (st * st);
            if (b2 < 0) continue;
            double be = sqrt(b2);
            px[n] = al; py[n] = be; n++;
            px[n] = al; py[n] = -be; n++;
        }
        for (int j = 0; j < 32; j++) {
            double psi = -M_PI + 2 * M_PI * j / 32, best = 1e9, rad = 5.196;
            /* nearest outline point in angle (outline is star-shaped about the hole) */
            for (int k = 0; k < n; k++) {
                double ang = atan2(py[k], px[k]), dd = fabs(remainder(ang - psi, 2 * M_PI));
                if (dd < best) { best = dd; rad = hypot(px[k], py[k]); }
            }
            tab[i * 32 + j] = (float)(rad * 0.5);     /* units of M -> r_s = 2M */
        }
    }
}
#endif
