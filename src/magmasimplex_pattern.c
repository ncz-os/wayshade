/* SPDX-License-Identifier: Apache-2.0 */
/* magmasimplex_pattern.c - see magmasimplex_pattern.h.
 *
 * Separate from magmasimplex_goo_sim.c on purpose: the wax-body simulation is
 * calibrated and gated (magmasimplex-randomize, 1000 seeds), and adding this
 * function to that translation unit perturbed its code enough to tip one
 * marginal blob-gate seed. Here it cannot affect the simulation at all. */
#include "magmasimplex_pattern.h"
#include <math.h>

static float urand(uint32_t *s) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return (float)(*s & 0xffffffu) / 16777216.f;
}

void ms_goo_pattern_seed(uint32_t seed, float pattern[4], float warp[4]) {
    uint32_t s = seed ? seed * 0x9E3779B1u + 0x1234567u : 0x1234567u;
    /* One avalanche first, so seeds that differ only in the low bits (the
     * long-run driver draws 32-bit seeds) still diverge clearly. */
    s ^= s >> 16; s *= 0x7FEB352Du; s ^= s >> 15; s *= 0x846CA68Bu; s ^= s >> 16;
    float a = urand(&s), b = urand(&s), c = urand(&s), d = urand(&s), e = urand(&s), f = urand(&s);
    pattern[0] = (a * 2.f - 1.f) * 3.0f;   /* noise-domain offset x */
    pattern[1] = (b * 2.f - 1.f) * 3.0f;   /* noise-domain offset y */
    pattern[2] = c * 6.2831853f;           /* rotation of the whole field */
    pattern[3] = 0.85f + 0.40f * d;        /* zoom: bounded 0.85..1.25 */
    warp[0]    = 0.60f + 1.00f * e;        /* warp amplitude: 0.6..1.6 */
    warp[1]    = cosf(f * 6.2831853f) * 0.6f;
    warp[2]    = sinf(f * 6.2831853f) * 0.6f;
    warp[3]    = e * 6.2831853f;           /* droplet / mask drift phase */
}
