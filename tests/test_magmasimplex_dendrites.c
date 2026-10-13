/* SPDX-License-Identifier: Apache-2.0 */
/* test_magmasimplex_dendrites.c - the Flowerchild dendrite option, the
 * projector-mask-size mapping and the resolve defaults. Pure C, no GL; the
 * frames themselves are measured by tools/magmasimplex-frame-gates.py.
 *
 * Covers:
 *   - ms_goo_dendrites_name;
 *   - ms_goo_mask_radius: 0 resolves to the new large default, a positive
 *     value is clamped to [0.30, 1.00], off/soft carry no radius, and
 *     fillmore-small (the legacy 0.47 dish) reproduces the old radius exactly;
 *   - ms_goo_cfg_resolve: dendrites off, strength 1 and mask-size 0 by default
 *     (no existing lamp preset changes). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "magmasimplex_goo_cfg.h"

static int near(float a, float b) { return fabsf(a - b) < 1e-5f; }

int main(void) {
    int fails = 0;
    if (strcmp(ms_goo_dendrites_name(MS_DEND_OFF), "off") ||
        strcmp(ms_goo_dendrites_name(MS_DEND_INK), "ink") ||
        strcmp(ms_goo_dendrites_name(MS_DEND_VEINS), "veins")) {
        printf("FAIL dendrites names\n");
        fails++;
    }
    /* The operator recorded the old small Fillmore dish as 0.47; the constant
     * is the single source of that value (the scene uses it too). */
    if (!near(MS_MASK_SIZE_LEGACY_DISH, 0.47f)) {
        printf("FAIL legacy dish constant %f\n", (double)MS_MASK_SIZE_LEGACY_DISH);
        fails++;
    }
    /* 0 = the style default (a large disc that fills most of the screen). */
    if (!near(ms_goo_mask_radius(MS_MASK_DISH, 0.f), MS_MASK_SIZE_DEFAULT) ||
        !near(ms_goo_mask_radius(MS_MASK_DISC, 0.f), MS_MASK_SIZE_DEFAULT)) {
        printf("FAIL 0 does not resolve to the new default\n");
        fails++;
    }
    /* fillmore-small reproduces the old radius exactly. */
    if (!near(ms_goo_mask_radius(MS_MASK_DISH, MS_MASK_SIZE_LEGACY_DISH), MS_MASK_SIZE_LEGACY_DISH)) {
        printf("FAIL fillmore-small radius\n");
        fails++;
    }
    /* roundhouse keeps its old, larger disc exactly. */
    if (!near(ms_goo_mask_radius(MS_MASK_DISC, MS_MASK_SIZE_LEGACY_DISC), MS_MASK_SIZE_LEGACY_DISC)) {
        printf("FAIL roundhouse radius\n");
        fails++;
    }
    /* Clamping. */
    if (!near(ms_goo_mask_radius(MS_MASK_DISH, 0.10f), MS_MASK_SIZE_MIN) ||
        !near(ms_goo_mask_radius(MS_MASK_DISH, 0.001f), MS_MASK_SIZE_MIN) ||
        !near(ms_goo_mask_radius(MS_MASK_DISC, 5.0f), MS_MASK_SIZE_MAX)) {
        printf("FAIL mask-radius clamp\n");
        fails++;
    }
    if (ms_goo_mask_radius(MS_MASK_OFF, 0.5f) != 0.f || ms_goo_mask_radius(MS_MASK_SOFT, 0.5f) != 0.f) {
        printf("FAIL off/soft carry no radius\n");
        fails++;
    }
    /* Resolve defaults: existing looks are unchanged. */
    MsGooConfig c;
    ms_goo_cfg_unset(&c);
    ms_goo_cfg_resolve(&c, 12345u);
    if (c.dendrites != MS_DEND_OFF || !near(c.dendrite_strength, 1.f) || !near(c.projector_mask_size, 0.f)) {
        char b[700];
        ms_goo_cfg_describe(&c, b, sizeof b);
        printf("FAIL resolve defaults: %s\n", b);
        fails++;
    }
    printf("magmasimplex dendrites: fails=%d default=%.2f legacy-dish=%.2f min=%.2f max=%.2f\n",
           fails, (double)MS_MASK_SIZE_DEFAULT, (double)MS_MASK_SIZE_LEGACY_DISH,
           (double)MS_MASK_SIZE_MIN, (double)MS_MASK_SIZE_MAX);
    return fails ? 1 : 0;
}
