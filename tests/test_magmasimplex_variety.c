/* SPDX-License-Identifier: Apache-2.0 */
/* test_magmasimplex_variety.c - pattern variety and determinism (2026-10-05).
 *
 * The operator saw the same Flowerchild/show marbling and the same Lava
 * arrangement at different palettes. Two properties must hold:
 *   determinism  the same seed resolves to the same pattern parameters and
 *                the same initial body layout, bit for bit;
 *   variety      different seeds differ clearly in both the light-show field
 *                layout (ms_goo_pattern_seed) and the wax-body layout the lamp
 *                views start from (ms_goo_sim_init_cap).
 * Pure C, no GL, so it runs headless.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "magmasimplex_goo_sim.h"
#include "magmasimplex_pattern.h"

static int fails;
static void fail(const char *fmt, unsigned seed) {
    printf("FAIL seed=%u %s\n", seed, fmt);
    fails++;
}

/* Mean absolute difference of the two parameter vectors. */
static float pdiff(const float *a, const float *b, int n) {
    float s = 0;
    for (int i = 0; i < n; i++) s += fabsf(a[i] - b[i]);
    return s / (float)n;
}

int main(void) {
    const int N = 64;
    float pat[64][4], warp[64][4];
    for (int i = 0; i < N; i++) {
        uint32_t seed = (uint32_t)(i + 1) * 2654435761u + 17u;
        ms_goo_pattern_seed(seed, pat[i], warp[i]);
        /* Bounds: the presets must stay recognisable whatever the seed. */
        if (pat[i][3] < 0.85f - 1e-4f || pat[i][3] > 1.25f + 1e-4f)
            fail("field zoom out of 0.85..1.25", seed);
        if (warp[i][0] < 0.60f - 1e-4f || warp[i][0] > 1.60f + 1e-4f)
            fail("warp amplitude out of 0.6..1.6", seed);
        if (fabsf(pat[i][2]) > 6.2831853f + 1e-3f)
            fail("rotation out of range", seed);
        float f2[4], w2[4];
        ms_goo_pattern_seed(seed, f2, w2);
        if (memcmp(pat[i], f2, sizeof f2) || memcmp(warp[i], w2, sizeof w2))
            fail("pattern not reproducible", seed);
    }
    /* Variety: every pair must differ in the offset/rotation/zoom, and the
     * spread must be well above a rendering-noise threshold. */
    float minoff = 1e9f, meanoff = 0.0f, minzoom = 1e9f;
    int pairs = 0, close = 0;
    for (int i = 0; i < N; i++) {
        for (int j = i + 1; j < N; j++) {
            float d = pdiff(pat[i], pat[j], 4);
            meanoff += d;
            if (d < minoff) minoff = d;
            if (fabsf(pat[i][3] - pat[j][3]) < minzoom) minzoom = fabsf(pat[i][3] - pat[j][3]);
            /* two seeds must not land on nearly the same field: a mean
             * parameter difference under 0.05 is a visible repeat. */
            if (d < 0.05f) close++;
            pairs++;
        }
    }
    meanoff /= (float)pairs;
    printf("pattern: pairs=%d mean_diff=%.3f min_diff=%.3f close=%d (limit 0)\n",
           pairs, meanoff, minoff, close);
    if (close != 0) fail("too many near-identical pattern pairs", 0);
    if (meanoff < 0.5f) fail("pattern spread too small", 0);

    /* Body layout: same seed identical, different seeds different. */
    const float asp = 16.f / 9.f;
    for (int i = 0; i < N; i++) {
        uint32_t seed = (uint32_t)(i + 1) * 40503u + 7u;
        MsGooSim a, b, c;
        ms_goo_sim_init_cap(&a, seed, 20, 1, asp, 90.f, 1);
        ms_goo_sim_init_cap(&b, seed, 20, 1, asp, 90.f, 1);
        if (memcmp(&a.b, &b.b, sizeof a.b) || memcmp(&a.s, &b.s, sizeof a.s))
            fail("body layout not reproducible", seed);
        uint32_t other = seed ^ 0x5bd1e995u;
        ms_goo_sim_init_cap(&c, other, 20, 1, asp, 90.f, 1);
        /* Compare the live bodies' positions and sizes. */
        float da = 0;
        int na = 0, diff = 0;
        for (int k = 0; k < MS_GOO_MAXB; k++) {
            if (!a.b[k].alive) continue;
            na++;
            float best = 1e9f;
            for (int m = 0; m < MS_GOO_MAXB; m++) {
                if (!c.b[m].alive) continue;
                float d = hypotf(a.b[k].x - c.b[m].x, a.b[k].y - c.b[m].y) +
                          fabsf(a.b[k].r - c.b[m].r);
                if (d < best) best = d;
            }
            da += best;
            if (best > 0.12f) diff++;
        }
        if (na == 0 || diff * 3 < na) {
            /* At least a third of the bodies should have no close match. */
            printf("FAIL seed=%u body layouts too similar (na=%d diff=%d)\n", seed, na, diff);
            fails++;
        }
    }
    printf("variety: fails=%d seeds=%d\n", fails, N);
    return fails ? 1 : 0;
}
