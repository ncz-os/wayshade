/* SPDX-License-Identifier: Apache-2.0 */
/* test_feed_render_fps.c - regression guard for ncz_feed_render_main()'s
 * per-frame cadence budget. The cadence budget divides by the effective
 * frame rate; that frame rate is derived from the configured fps, the
 * warm-band fps, and the current thermal level. Any future relaxation of
 * the NCZ_FEED_FPS / NCZ_FEED_FPS_WARM clamps must not be allowed to make
 * that divisor zero.
 *
 * Two checks: the helper itself (covers normal, warm, hot and the negative
 * cases the helper has to absorb) and a static-style budget sample that
 * proves a 1.0 / fps_eff budget is finite for every input the loop can
 * legally produce.
 */
#include <math.h>
#include <stdio.h>

#include "ncz_feed_render.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void) {
    /* Normal level: warm_fps is irrelevant (the slow branch only fires when
     * warm_fps < fps AND the level is 1). */
    CHECK(ncz_feed_effective_fps(30, 15, 0) == 30);
    CHECK(ncz_feed_effective_fps(30, 60, 0) == 30);          /* warm_fps > fps still ignored */
    CHECK(ncz_feed_effective_fps(30, 30, 0) == 30);          /* equal */

    /* Warm level: warm_fps wins only when it is strictly lower. */
    CHECK(ncz_feed_effective_fps(30, 15, 1) == 15);
    CHECK(ncz_feed_effective_fps(30, 30, 1) == 30);          /* equal, warm branch skipped */
    CHECK(ncz_feed_effective_fps(30, 60, 1) == 30);          /* higher, warm branch skipped */

    /* Hot (paused): the helper still has to be safe; the loop short-circuits
     * the cadence budget when level == 2, but a future caller should never
     * have to second-guess the helper. */
    CHECK(ncz_feed_effective_fps(30, 15, 2) == 30);

    /* Negative: future removal of the env-var clamp must not divide by zero. */
    CHECK(ncz_feed_effective_fps(0, 0, 0) == 1);             /* both zero, level 0 -> 1 */
    CHECK(ncz_feed_effective_fps(0, 0, 1) == 1);             /* both zero, level 1 -> 1 */
    CHECK(ncz_feed_effective_fps(0, 5, 1) == 1);             /* fps=0, warm branch with v=0 -> 1 */
    CHECK(ncz_feed_effective_fps(0, -7, 0) == 1);            /* negative guard */
    CHECK(ncz_feed_effective_fps(-3, 0, 0) == 1);            /* negative fps */
    CHECK(ncz_feed_effective_fps(30, -1, 1) == 1);           /* warm branch picks negative -> 1 */

    /* The cadence budget the loop computes (1.0 / fps_eff) must be finite and
     * positive for every legal input. */
    int cases[][3] = {
        {30, 15, 0}, {30, 15, 1}, {30, 15, 2},
        {0, 0, 0}, {0, 0, 1}, {0, 0, 2},
        {-1, -1, 0}, {240, 240, 1},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int fps = cases[i][0], warm = cases[i][1], level = cases[i][2];
        int eff = ncz_feed_effective_fps(fps, warm, level);
        CHECK(eff >= 1);
        double budget = 1.0 / (double)eff;
        CHECK(isfinite(budget) && budget > 0.0);
    }

    if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
    printf("feed_render_fps: all checks passed\n");
    return 0;
}