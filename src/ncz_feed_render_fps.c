/* SPDX-License-Identifier: Apache-2.0 */
/* ncz_feed_render_fps.c - cadence helper, separated from ncz_feed_render.c so
 * the unit test can link it without dragging the entire EGL/GLES/GBM stack in.
 * Keep this file header-only-equivalent: no static state, no GL calls, no
 * includes beyond stdint.h / math.h.
 */
#include "ncz_feed_render.h"

#include <math.h>

int ncz_feed_effective_fps(int fps, int warm_fps, int level) {
    int v = (level == 1 && warm_fps < fps) ? warm_fps : fps;
    return v < 1 ? 1 : v;
}

void ncz_feed_adapt_classify(double dt_ms, double panel_period_ms,
                             long long effective_budget_ns,
                             int *out_miss, int *out_near)
{
    /* Standalone / on-screen run: ncz_time_offset is not in use, the controller
     * keeps its original behaviour against the panel period alone. */
    double target_ms = panel_period_ms;
    if (effective_budget_ns > 0) {
        double feed_ms = (double)effective_budget_ns / 1.0e6;
        if (feed_ms > target_ms) target_ms = feed_ms;
    }
    if (panel_period_ms <= 0.0) panel_period_ms = target_ms;   /* defensive */
    int miss = dt_ms > target_ms * 1.30;
    int near_ = dt_ms > target_ms * 1.08;
    if (out_miss) *out_miss = miss;
    if (out_near) *out_near = near_;
}