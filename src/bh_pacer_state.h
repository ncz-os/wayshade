/* SPDX-License-Identifier: Apache-2.0 */
/* bh_pacer_state.h - the small seam the BH adaptive-quality controller
 * (bh_pace, gles3_blackhole.c) and its unit test share. Extracted so the
 * regression test exercises the SAME struct / reset function the saver wires
 * into its draw loop, not a copy. Header-only, no allocations, no GL calls.
 *
 * The reset clears the budget-specific failure history (miss / near rings,
 * running max dt, last-step fail level) WITHOUT touching the current ray
 * level - that is the explicit requirement when the active feed cadence
 * transitions (warm-15 <-> capped-30 <-> capped-60): the previous mode's
 * misses were computed against the wrong target and would otherwise poison
 * the new mode's first 60-frame window. The saver's quality level itself
 * is preserved.
 */
#ifndef NCZ_BH_PACER_STATE_H
#define NCZ_BH_PACER_STATE_H

#include <string.h>
#include "ncz_feed_render.h"

typedef struct {
    unsigned char mring[60];
    int mcnt;
    unsigned char nring[60];
    int ncnt;
    float dtmax;
    float cur_fail_level;
} BhPacerState;

/* Production reset: zero the miss / near rings + counts, reset the running
 * max dt and the cached failure level. The saver's ray-step level lives
 * outside this struct and is preserved by the caller. */
static inline void bh_pacer_state_reset(BhPacerState *p) {
    memset(p->mring, 0, sizeof p->mring);
    memset(p->nring, 0, sizeof p->nring);
    p->mcnt = 0;
    p->ncnt = 0;
    p->dtmax = 0.f;
    p->cur_fail_level = 0.f;
}

/* The actual per-frame policy used by BH. Warmup is observational only;
 * the interval ending at a cadence transition belongs to the old mode and
 * is excluded from the new mode's history. */
static inline void bh_pacer_state_update(BhPacerState *p, unsigned long frame,
                                         double dt_ms, double panel_ms,
                                         long long budget_ns, int changed) {
    if (changed) {
        bh_pacer_state_reset(p);
        return;
    }
    int miss = 0, near = 0;
    if (frame > 150)
        ncz_feed_adapt_classify(dt_ms, panel_ms, budget_ns, &miss, &near);
    unsigned int i = (unsigned int)(frame % 60);
    p->mcnt += miss - p->mring[i];
    p->mring[i] = (unsigned char)miss;
    p->ncnt += near - p->nring[i];
    p->nring[i] = (unsigned char)near;
    if (dt_ms > p->dtmax) p->dtmax = (float)dt_ms;
}

#endif /* NCZ_BH_PACER_STATE_H */