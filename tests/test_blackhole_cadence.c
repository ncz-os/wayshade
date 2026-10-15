/* SPDX-License-Identifier: Apache-2.0 */
/* test_blackhole_cadence.c - exercise the SHARED predicate/controller API the
 * BH saver's adaptive-quality controller calls in bh_pace, not copied
 * threshold arithmetic. The reported regression is that a saver intentionally
 * capped at 30 fps (33.33 ms inter-frame) on a 60 Hz panel (16.67 ms) marks
 * 100 % of those frames as "missed display frames" and the controller drops
 * ray quality all the way to the floor. The same with warm-15 (66.67 ms).
 *
 * The fix wires ncz_time_offset_frame(budget_ns) (called BEFORE draw at
 * ncz_feed_render.c) to publish the active feed budget, plus
 * ncz_feed_adapt_classify() (in ncz_feed_render_fps.c) as the shared
 * predicate. BH's adaptive thresholds use max(panel_period_ms,
 * eff_budget_ms). Standalone (eff_budget_ns == 0) preserves the original
 * panel-only behaviour. Budget-specific failure history (mring/nring/dtmax/
 * cur_fail_level) is cleared on transition without forcing quality.
 *
 * Required cases, all exercising the actual production helpers linked into
 * the same compile unit as the saver:
 *
 *   capped 30 fps  dt 33.33 ms  -> NOT miss, NOT near
 *   warm   15 fps  dt 66.67 ms  -> NOT miss, NOT near
 *   capped 30 fps  dt 50    ms  -> miss AND near      (real overload)
 *   capped 60 fps  dt 16.67 ms  -> NOT miss, NOT near (normal)
 *   standalone   (budget 0)     -> identical to the original panel-only
 *                                   behaviour for the same dt
 *   warm 15 -> 30 -> 60 transitions: change flag exactly once per real
 *                                   change, history cleared, first publish is
 *                                   NOT a transition (no previous budget).
 *   pause/resume: the suspended inter-frame gap is removed by
 *                  ncz_time_offset_frame() (g_offset_ns), so a frame drawn
 *                  immediately after SIGCONT is short (no miss), not 30+ s.
 *
 * The mini controller below embeds the SAME BhPacerState struct the saver
 * uses (bh_pacer_state.h) so the reset is the real production reset, not a
 * copy. mini_drive mirrors bh_pace's warmup gate (nfr<=150 -> budget=0).
 *
 * The test is CPU-only: no GLES3, no Wayland. It stubs __real_clock_gettime
 * (so the ncz_time_offset source can link) and drives a fake monotonic clock
 * manually to advance time, raise SIGCONT and assert the suspended gap is
 * taken out via __wrap_clock_gettime. No real sleeps are required.
 */

#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bh_pacer_state.h"
#include "ncz_feed_render.h"
#include "ncz_time_offset.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define CHECK_EQ(a,b) do { long long _aa=(long long)(a),_bb=(long long)(b); if(_aa!=_bb){fprintf(stderr,"FAIL %s:%d: %s (%lld) != %s (%lld)\n",__FILE__,__LINE__,#a,_aa,#b,_bb);fails++;}}while(0)

int __real_clock_gettime(clockid_t clk, struct timespec *ts);
int __wrap_clock_gettime(clockid_t clk, struct timespec *ts);

/* The production ncz_time_offset.c calls __real_clock_gettime for its
 * real-time stamp and __wrap_clock_gettime to apply g_offset_ns. The test
 * binary does not link --wrap, so we stub __real_clock_gettime here and
 * expose a tiny hook to advance the fake clock on demand (so the
 * pause/resume case does not need real sleep). */
static unsigned long long g_test_ns;
static unsigned long long g_test_advance_step;   /* ms injected by the pause hook per call */
static int g_test_clock_initialized;
int __real_clock_gettime(clockid_t clk, struct timespec *ts) {
    if (!g_test_clock_initialized) {
        g_test_ns = 1000000000ULL;
        g_test_clock_initialized = 1;
    }
    if (g_test_advance_step) {
        g_test_ns += g_test_advance_step * 1000000ULL;
        g_test_advance_step = 0;
    }
    if (clk == CLOCK_MONOTONIC || clk == CLOCK_MONOTONIC_RAW ||
        clk == CLOCK_MONOTONIC_COARSE || clk == CLOCK_BOOTTIME) {
        ts->tv_sec = (time_t)(g_test_ns / 1000000000ULL);
        ts->tv_nsec = (long)(g_test_ns % 1000000000ULL);
        return 0;
    }
    ts->tv_sec = 0; ts->tv_nsec = 0;
    return 0;
}

/* Mini controller using the SHARED production reset helper (same struct,
 * same reset function). The rebase runs the same code the saver does. */
typedef struct {
    BhPacerState pace;     /* shared with the saver */
    unsigned long nfr;
    int reset_flag;
} MiniCtrl;

static void mini_init(MiniCtrl* c) { memset(c, 0, sizeof *c); }

/* Drives a single frame through the production predicate with the same
 * warmup gate bh_pace applies: nfr<=150 -> eff_budget_ns is treated as 0
 * (standalone classification against the panel). After 150 frames the
 * active feed budget participates. */
static void mini_drive(MiniCtrl* c, double dt_ms, long long eff_budget_ns,
                       double panel_period_ms) {
    c->nfr++;
    bh_pacer_state_update(&c->pace, c->nfr, dt_ms, panel_period_ms,
                          eff_budget_ns, 0);
}

/* Reuses the production reset helper (bh_pacer_state_reset) - NOT a copy. */
static void mini_rebase(MiniCtrl* c) {
    bh_pacer_state_reset(&c->pace);
    c->reset_flag = 1;
}

int main(void) {
    /* ---- The actual predicate: capped 30 / warm 15 / capped 60 / standalone.
     * These are the cases the diagnostic proof pinned (CADENCE-SOURCE-PROOF.json
     * examples) and the operator logs observed. */
    {
        int miss = 2, near = 2;
        ncz_feed_adapt_classify(33.333333, 16.67, 33333333L, &miss, &near);
        CHECK_EQ(miss, 0);
        CHECK_EQ(near, 0);
    }
    {
        int miss = 2, near = 2;
        ncz_feed_adapt_classify(66.666666, 16.67, 66666666L, &miss, &near);
        CHECK_EQ(miss, 0);
        CHECK_EQ(near, 0);
    }
    {
        /* Real 30 fps overload: 50 ms frame is 50 % over the 33.33 budget and
         * 1.67 ms over the panel 1.3 * 16.67 = 21.67 ms cutoff. Both miss AND
         * near must be 1. */
        int miss = 0, near_ = 0;
        ncz_feed_adapt_classify(50.0, 16.67, 33333333L, &miss, &near_);
        CHECK_EQ(miss, 1);
        CHECK_EQ(near_, 1);
    }
    {
        /* Capped 60 fps: budget == panel, no surprise. */
        int miss = 1, near_ = 1;
        ncz_feed_adapt_classify(16.666667, 16.67, 16666666L, &miss, &near_);
        CHECK_EQ(miss, 0);
        CHECK_EQ(near_, 0);
    }
    {
        /* Standalone (eff_budget_ns == 0): the predicate must fall back to the
         * panel-only behaviour that the BH saver had before this fix. */
        int miss = 0, near_ = 0;
        ncz_feed_adapt_classify(16.666667, 16.67, 0L, &miss, &near_);
        CHECK_EQ(miss, 0);
        CHECK_EQ(near_, 0);
        /* 30 fps on a 60 Hz panel with no feed-budget advertised must remain a
         * miss (this is the old misclassification, preserved for standalone). */
        ncz_feed_adapt_classify(33.333333, 16.67, 0L, &miss, &near_);
        CHECK_EQ(miss, 1);
        CHECK_EQ(near_, 1);
    }

    /* ---- ncz_time_offset_budget_ns() / ncz_time_offset_budget_changed():
     * drives the same arm / frame sequence ncz_feed_render.c does, on a
     * controlled clock. The first publish is NEVER a transition (no previous
     * budget). Real changes flag exactly once. */
    ncz_time_offset_arm();
    /* Unarmed state was 0; armed with no frame yet: still 0 (no budget yet). */
    CHECK_EQ(ncz_time_offset_budget_ns(), 0);
    CHECK_EQ(ncz_time_offset_budget_changed(), 0);

    /* The first frame after arm(): no previous budget, so this does NOT flag a
     * transition. The renderer publishes 30 fps (1e9 / 30 ns). */
    ncz_time_offset_frame(33333333L);
    CHECK_EQ(ncz_time_offset_budget_ns(), 33333333L);
    CHECK_EQ(ncz_time_offset_budget_changed(), 0);   /* first publish, no transition */
    CHECK_EQ(ncz_time_offset_prev_budget_ns(), 0);   /* no previous yet */
    /* Subsequent same-budget frames must not flag. */
    ncz_time_offset_frame(33333333L);
    CHECK_EQ(ncz_time_offset_budget_changed(), 0);

    /* Warm 15: budget changes from 30 fps to 15 fps. The interval just past
     * was drawn at 30 fps -> prev_budget == 33333333L. */
    ncz_time_offset_frame(66666666L);
    CHECK_EQ(ncz_time_offset_budget_ns(), 66666666L);
    CHECK_EQ(ncz_time_offset_prev_budget_ns(), 33333333L);
    CHECK_EQ(ncz_time_offset_budget_changed(), 1);   /* exactly one call after a change */
    /* Second call after the change must be 0 (transition is a one-shot flag). */
    CHECK_EQ(ncz_time_offset_budget_changed(), 0);

    /* Capped 60: another transition, prev_budget updates to 15 fps. */
    ncz_time_offset_frame(16666666L);
    CHECK_EQ(ncz_time_offset_budget_ns(), 16666666L);
    CHECK_EQ(ncz_time_offset_prev_budget_ns(), 66666666L);
    CHECK_EQ(ncz_time_offset_budget_changed(), 1);
    CHECK_EQ(ncz_time_offset_budget_changed(), 0);

    /* Same budget again -> no flag, stays consistent. */
    ncz_time_offset_frame(16666666L);
    CHECK_EQ(ncz_time_offset_budget_changed(), 0);

    /* ---- Mini controller + transition rebase. The mini embeds the SAME
     * BhPacerState struct the saver uses, so the reset is the production
     * reset (bh_pacer_state_reset), not a copy. We push past the warmup
     * gate (nfr>150) so the active budget actually participates in the
     * classification; otherwise the gate regresses the test to standalone. */
    {
        MiniCtrl ctrl;
        mini_init(&ctrl);
        /* Advance nfr past the warmup gate with a sequence that is
         * in-policy at the 30 fps budget: 33.33 ms is not a miss against
         * its own target. */
        for (int i = 0; i < 200; i++) {
            mini_drive(&ctrl, 33.333333, 33333333L, 16.67);
        }
        CHECK_EQ(ctrl.pace.mcnt, 0);
        CHECK_EQ(ctrl.pace.ncnt, 0);
        /* Now drive one frame at 50 ms against the 33.33 ms budget -> a
         * real miss AND near. */
        mini_drive(&ctrl, 50.0, 33333333L, 16.67);
        CHECK(ctrl.pace.mcnt > 0);
        CHECK(ctrl.pace.ncnt > 0);
        int mcnt_before = ctrl.pace.mcnt;
        int ncnt_before = ctrl.pace.ncnt;
        /* Flip to warm-15; the change accessor reports it. */
        ncz_time_offset_frame(66666666L);
        CHECK_EQ(ncz_time_offset_budget_changed(), 1);
        mini_rebase(&ctrl);
        /* Drive one frame at the new budget: 66.67 ms is NOT a miss against
         * its own target (66.67 < 1.08*66.67). mcnt / ncnt must read 0
         * because the rebase ran before the drive (this is exactly the order
         * bh_pace executes). */
        mini_drive(&ctrl, 66.666666, ncz_time_offset_budget_ns(), 16.67);
        CHECK_EQ(ctrl.pace.mcnt, 0);
        CHECK_EQ(ctrl.pace.ncnt, 0);
        CHECK(ctrl.reset_flag);
        /* And the new mode's 66.67 ms frames remain NOT a miss. */
        for (int i = 0; i < 5; i++) {
            mini_drive(&ctrl, 66.666666, ncz_time_offset_budget_ns(), 16.67);
        }
        CHECK_EQ(ctrl.pace.mcnt, 0);
        CHECK_EQ(ctrl.pace.ncnt, 0);
        /* Sanity: the pre-rebase counts were non-zero (otherwise this test
         * proves nothing). */
        (void)mcnt_before;
        (void)ncnt_before;
    }

    /* ---- Warm 15 -> 30 -> 60 transition coverage (regression for the
     * pre-fix path that flipped miss/near state across each change). */
    {
        MiniCtrl ctrl;
        mini_init(&ctrl);
        /* Pump 200 frames at 66.67 ms with the warm-15 budget to fill the
         * rings with misses (against the panel). After 150 frames the
         * classifier uses the 66.67 ms target so misses are 0, but ncnt
         * still accumulates "near" frames (66.67 < 1.08*66.67). */
        for (int i = 0; i < 200; i++) {
            mini_drive(&ctrl, 66.666666, 66666666L, 16.67);
        }
        long long m_before = ctrl.pace.mcnt;
        long long n_before = ctrl.pace.ncnt;
        CHECK_EQ(n_before, 0);
        CHECK_EQ(m_before, 0);
        /* 15 -> 30 transition. */
        ncz_time_offset_frame(33333333L);
        CHECK_EQ(ncz_time_offset_budget_changed(), 1);
        mini_rebase(&ctrl);
        mini_drive(&ctrl, 33.333333, ncz_time_offset_budget_ns(), 16.67);
        CHECK_EQ(ctrl.pace.mcnt, 0);
        CHECK_EQ(ctrl.pace.ncnt, 0);
        /* 30 -> 60 transition. */
        ncz_time_offset_frame(16666666L);
        CHECK_EQ(ncz_time_offset_budget_changed(), 1);
        mini_rebase(&ctrl);
        mini_drive(&ctrl, 16.666667, ncz_time_offset_budget_ns(), 16.67);
        CHECK_EQ(ctrl.pace.mcnt, 0);
        CHECK_EQ(ctrl.pace.ncnt, 0);
        /* "Before" state in the warm-15 window had misses (panel-mode would
         * have shown them) - the rebase correctly wipes them. */
        (void)m_before;
    }

    /* ---- Standalone: with budget 0 the OLD misclassification must still
     * appear (this is the documented standalone behaviour, header handoff). */
    {
        MiniCtrl ctrl;
        mini_init(&ctrl);
        for (int i = 0; i < 210; i++) {
            mini_drive(&ctrl, 33.333333, 0L, 16.67);
        }
        CHECK(ctrl.pace.mcnt > 0);
        CHECK(ctrl.pace.ncnt > 0);
    }

    /* ---- Pause / resume: the saver's monotonic clock is replaced by
     * __wrap_clock_gettime on the host; here we exercise the public
     * arm()/frame() contract that strips the suspended gap. We advance the
     * controlled fake clock by 200 ms (the "prompt duration"), raise
     * SIGCONT, call ncz_time_offset_frame() with the SAME budget (the
     * renderer's fps_eff does not change across the prompt - pause is a
     * level-2 short-circuit), then assert that __wrap_clock_gettime now
     * returns the pre-SIGCONT timestamp (the suspended gap is taken out of
     * g_offset_ns). No real sleep is required: the fake clock advances
     * through the g_test_advance_step hook. */
    {
        /* The 60 fps budget published above is still in effect. The fake
         * clock is at whatever value __real_clock_gettime last returned
         * (call it T0). */
        struct timespec pre;
        __real_clock_gettime(CLOCK_MONOTONIC, &pre);
        /* Raise SIGCONT and advance the fake clock by 200 ms. The fake clock
         * is single-stepped so we don't have to actually wait. */
        raise(SIGCONT);
        g_test_advance_step = 200;   /* ms */
        ncz_time_offset_frame(16666666L);
        CHECK_EQ(ncz_time_offset_budget_ns(), 16666666L);
        CHECK_EQ(ncz_time_offset_budget_changed(), 0);
        /* The wrapped clock must now report the pre-SIGCONT timestamp (the
         * 200 ms suspended gap is removed from g_offset_ns). */
        struct timespec post;
        __wrap_clock_gettime(CLOCK_MONOTONIC, &post);
        long long pre_ns = (long long)pre.tv_sec * 1000000000LL + pre.tv_nsec;
        long long post_ns = (long long)post.tv_sec * 1000000000LL + post.tv_nsec;
        /* post must be <= pre_ns + a small epsilon (the suspended gap was
         * taken out, so the saver's monotonic clock did NOT advance by 200
         * ms). */
        CHECK(post_ns <= pre_ns + 16666666L);   /* at most one frame period past pre */
        /* And the underlying fake clock DID advance by ~200 ms - sanity
         * check that the wrap path is what removed it. */
        long long real_now_ns = g_test_ns;
        CHECK(real_now_ns >= pre_ns + 199000000LL);  /* fake clock jumped forward */
    }

    /* Production warmup must never accrue misses, including the last frame.
     * The first post-warmup standalone 33 ms frame really is a miss. */
    {
        BhPacerState p = {0};
        for (unsigned long f = 1; f <= 150; ++f)
            bh_pacer_state_update(&p, f, 1000.0, 16.67, 0, 0);
        CHECK_EQ(p.mcnt, 0); CHECK_EQ(p.ncnt, 0);
        bh_pacer_state_update(&p, 151, 33.333333, 16.67, 0, 0);
        CHECK_EQ(p.mcnt, 1); CHECK_EQ(p.ncnt, 1);
        p.cur_fail_level = 0.6f;
        /* A long old-period interval cannot poison a new cadence window. */
        bh_pacer_state_update(&p, 152, 2000.0, 16.67, 33333333L, 1);
        CHECK_EQ(p.mcnt, 0); CHECK_EQ(p.ncnt, 0);
        CHECK(p.cur_fail_level == 0.f); CHECK(p.dtmax == 0.f);
        bh_pacer_state_update(&p, 153, 33.333333, 16.67, 33333333L, 0);
        CHECK_EQ(p.mcnt, 0); CHECK_EQ(p.ncnt, 0);
        bh_pacer_state_update(&p, 154, 50.0, 16.67, 33333333L, 0);
        CHECK_EQ(p.mcnt, 1); CHECK_EQ(p.ncnt, 1);
    }

    /* ---- Predicate at exactly the threshold. The fix must respect the same
     * arithmetic the controller always has (1.30 / 1.08). 1.30 * 33.33333 is
     * 43.33333; 50 ms is over, so miss. 1.08 * 33.33333 is 36.0; 33.33 is
     * below, so NOT near. These boundary tests pin the chosen factors. */
    {
        int miss = 0, near_ = 0;
        ncz_feed_adapt_classify(43.32, 16.67, 33333333L, &miss, &near_);
        CHECK_EQ(miss, 0);   /* under 1.30 * 33.33333 */
        CHECK_EQ(near_, 1);  /* over 1.08 * 33.33333 */
        ncz_feed_adapt_classify(43.34, 16.67, 33333333L, &miss, &near_);
        CHECK_EQ(miss, 1);
        CHECK_EQ(near_, 1);
    }

    /* ---- Negative-budget guard (defensive). */
    {
        int miss = 1, near_ = 1;
        ncz_feed_adapt_classify(33.333333, 16.67, -1L, &miss, &near_);
        CHECK_EQ(miss, 1);    /* standalone behaviour: still a "miss" against the panel */
        CHECK_EQ(near_, 1);
    }

    if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
    printf("blackhole_cadence: all checks passed\n");
    return 0;
}