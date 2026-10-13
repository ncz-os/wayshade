/* SPDX-License-Identifier: Apache-2.0 */
/* magmasimplex_pattern.h - seed-derived layout of the light-show dye field.
 * Pure C, no GL. Kept in its own translation unit so the wax-body simulation
 * (magmasimplex_goo_sim.c) stays byte-identical to the calibrated baseline. */
#ifndef MAGMASIMPLEX_PATTERN_H
#define MAGMASIMPLEX_PATTERN_H
#include <stdint.h>

/* Seed-dependent layout of the light-show dye field (goo.frag u_pattern and
 * u_warp): noise-domain offset, rotation and zoom, warp amplitude and flow
 * direction, and the droplet/projector-mask phase. Bounded so a preset keeps
 * its look while every seed lays the field out differently; deterministic so
 * the same seed reproduces exactly. */
void ms_goo_pattern_seed(uint32_t seed, float pattern[4], float warp[4]);

#endif
