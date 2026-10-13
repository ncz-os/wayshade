/* SPDX-License-Identifier: Apache-2.0 */
/* test_magmasimplex_randomize.c - randomize mode: 1000 seeded draws.
 *
 * For each seed: resolve every setting from the seed (as a launch with
 * nothing set does), then check
 *   - the draw is reproducible (the same seed resolves identically);
 *   - every setting is inside its option range;
 *   - a drawn palette passes the readability rule;
 *   - the three depth layers, built as the saver builds them, keep at least
 *     three separate masses in the far layer (the one the field view shows)
 *     at 20, 60, 120 and 300 s of wall time, at the drawn speed;
 * and every palette id in the option's choices resolves to a table entry.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "magmasimplex_goo_cfg.h"
#include "magmasimplex_goo_sim.h"
#include "magmasimplex_palettes.h"

static int in(float v, float a, float b) { return v >= a - 1e-4f && v <= b + 1e-4f; }

int main(void) {
    int fails = 0, draws = 1000;
    int worlds[4] = {0}, mats[3] = {0}, gen = 0;
    /* every id in the choices is a known palette */
    char buf[8192];
    snprintf(buf, sizeof buf, "%s", ms_palette_choices());
    int ids = 0;
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        if (!strcmp(tok, "auto") || !strncmp(tok, "generated-", 10)) continue;
        ids++;
        if (ms_palette_find(tok) < 0) { printf("FAIL unknown palette id in choices: %s\n", tok); fails++; }
    }
    if (ids != ms_palette_count()) { printf("FAIL choices list %d ids, table %d\n", ids, ms_palette_count()); fails++; }
    for (int i = 0; i < draws; i++) {
        uint32_t seed = (uint32_t)i * 2654435761u + 17u;
        MsGooConfig c, c2;
        ms_goo_cfg_unset(&c);
        ms_goo_cfg_resolve(&c, seed);
        ms_goo_cfg_unset(&c2);
        ms_goo_cfg_resolve(&c2, seed);
        char a[700], b[700];
        ms_goo_cfg_describe(&c, a, sizeof a);
        ms_goo_cfg_describe(&c2, b, sizeof b);
        if (strcmp(a, b)) { printf("FAIL seed %u not reproducible\n", seed); fails++; }
        if (!in(c.texture, 0, 2) || !in(c.viscosity, 0.5f, 2) || !in(c.heat, 0.5f, 1.5f) || !in(c.wax, 0.6f, 1.4f) ||
            !in(c.speed, 0.1f, 8) || !in(c.motes, 0, 2) || !in(c.glass, 0, 2) || !in(c.depth, 0.3f, 2) ||
            !in(c.drift, 0, 2) || c.world < 0 || c.world > 3 || c.material < 0 || c.material > 2) {
            printf("FAIL seed %u out of range: %s\n", seed, a);
            fails++;
        }
        worlds[c.world]++;
        mats[c.material]++;
        MsPalette p;
        ms_goo_cfg_palette(&c, seed, &p);
        if (c.palette < 0) gen++;
        if (!ms_palette_readable(&p)) { printf("FAIL seed %u palette %s not readable\n", seed, p.id); fails++; }
        /* the far layer, as init_sims builds it (full quality, 16:9 screen) */
        float asp = c.world == MS_WORLD_DISH ? 4.f / 3.f : (c.world == MS_WORLD_RADIAL ? 1.78f : 16.f / 9.f);
        MsGooSim s;
        ms_goo_sim_init_cap(&s, seed, 12, (int)(seed & 1u), asp, 90.f, c.cap);
        s.visc_k = c.viscosity;
        s.heat_k = c.heat;
        s.wax_k = c.wax;
        const float checks[] = {20, 60, 120, 300};
        float sdt = (1.f / 30.f) * c.speed * ms_goo_time_scale();
        int ci = 0;
        for (int f = 0; f <= 300 * 30 && ci < 4; f++) {
            float t = f / 30.f;
            if (t >= checks[ci]) {
                int v = ms_goo_sim_visible(&s);
                if (v < 3) {
                    printf("FAIL seed %u t=%.0f visible=%d %s\n", seed, t, v, a);
                    fails++;
                }
                ci++;
            }
            ms_goo_sim_step(&s, sdt);
        }
    }
    printf("draws=%d fails=%d palettes=%d generated=%d worlds tank/dish/radial/drift=%d/%d/%d/%d "
           "materials milky/translucent/jelly=%d/%d/%d\n",
           draws, fails, ms_palette_count(), gen, worlds[0], worlds[1], worlds[2], worlds[3], mats[0], mats[1], mats[2]);
    return fails ? 1 : 0;
}
