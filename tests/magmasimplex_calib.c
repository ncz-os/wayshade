/* SPDX-License-Identifier: Apache-2.0 */
/* magmasimplex_calib.c - report the goo simulation's motion statistics in
 * the same terms tools/magmasimplex-footage-stats.py measures real lamps in,
 * for the "real lamp vs simulation" table in docs/MAGMASIMPLEX-60S.md.
 *
 * Heights are screen (simulation) heights: y spans 2 units. Speeds are for
 * free bodies (not budding, not draining, clear of the pool and the cap),
 * sampled at 2 Hz, in heights per second of SIMULATION time; divide by the
 * saver's time scale to get the on-screen rate.
 *
 * usage: magmasimplex_calib [seconds] [budget] [asp] [cap] [seeds] [visc] [heat]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "magmasimplex_goo_sim.h"

static int cmpf(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}
static float pct(float *v, int n, float p) {
    if (n <= 0) return NAN;
    qsort(v, (size_t)n, sizeof *v, cmpf);
    int i = (int)(p / 100.f * (float)(n - 1) + 0.5f);
    return v[i];
}

int main(int argc, char **argv) {
    float secs = argc > 1 ? (float)atof(argv[1]) : 1800.f;
    int budget = argc > 2 ? atoi(argv[2]) : 12;
    float asp = argc > 3 ? (float)atof(argv[3]) : 16.f / 9.f;
    int cap = argc > 4 ? atoi(argv[4]) : 1;
    int seeds = argc > 5 ? atoi(argv[5]) : 8;
    float visc = argc > 6 ? (float)atof(argv[6]) : 1.f;
    float heat = argc > 7 ? (float)atof(argv[7]) : 1.f;
    int cap_n = (int)(secs * 2.f) * MS_GOO_MAXB * seeds + 16;
    float *up = malloc(sizeof(float) * (size_t)cap_n), *dn = malloc(sizeof(float) * (size_t)cap_n);
    float *dia = malloc(sizeof(float) * (size_t)cap_n), *vis = malloc(sizeof(float) * (size_t)cap_n);
    int nu = 0, nd = 0, ndia = 0, nv = 0;
    for (int sd = 1; sd <= seeds; sd++) {
        MsGooSim s;
        ms_goo_sim_init_cap(&s, (uint32_t)sd * 2654435761u, budget, sd & 1, asp, 90.f, cap);
        s.visc_k = visc;
        s.heat_k = heat;
        float prevy[MS_GOO_MAXB];
        int prevok[MS_GOO_MAXB] = {0};
        for (int f = 0; f <= (int)(secs * 30.f); f++) {
            if (f % 15 == 0) {
                vis[nv++] = (float)ms_goo_sim_visible(&s);
                for (int i = 0; i < MS_GOO_MAXB; i++) {
                    const MsGooBody *b = &s.b[i];
                    int ok = b->alive && b->mode == 0 && b->y - b->r > MS_GOO_POOL_Y + 0.12f && b->y + b->r < 0.84f;
                    if (ok) {
                        dia[ndia++] = b->r; /* 2r / height 2 */
                        if (prevok[i]) {
                            float v = (b->y - prevy[i]) / 2.f / 0.5f;
                            if (v > 0.002f) up[nu++] = v;
                            else if (v < -0.002f) dn[nd++] = -v;
                        }
                    }
                    prevok[i] = ok;
                    prevy[i] = b->y;
                }
            }
            ms_goo_sim_step(&s, 1.f / 30.f);
        }
    }
    float mv = 0;
    for (int i = 0; i < nv; i++) mv += vis[i];
    int none = 0;
    for (int i = 0; i < nv; i++) none += vis[i] < 0.5f;
    printf("budget=%d asp=%.2f cap=%d seeds=%d secs=%.0f visc=%.2f heat=%.2f\n", budget, asp, cap, seeds, secs, visc, heat);
    printf(" separate masses: mean=%.2f none=%.2f%%\n", mv / (float)nv, 100.f * (float)none / (float)nv);
    printf(" diameter/height: p10=%.3f p50=%.3f p90=%.3f\n", pct(dia, ndia, 10), pct(dia, ndia, 50), pct(dia, ndia, 90));
    printf(" rise speed (heights/s sim): p10=%.4f p50=%.4f p90=%.4f (n=%d)\n", pct(up, nu, 10), pct(up, nu, 50),
           pct(up, nu, 90), nu);
    printf(" sink speed (heights/s sim): p10=%.4f p50=%.4f p90=%.4f (n=%d)\n", pct(dn, nd, 10), pct(dn, nd, 50),
           pct(dn, nd, 90), nd);
    free(up); free(dn); free(dia); free(vis);
    return 0;
}
