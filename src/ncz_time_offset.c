// SPDX-License-Identifier: Apache-2.0
//
// ncz_time_offset.c - a saver resumes where it was suspended.
//
// While the unlock prompt is up the lock suspends the saver (the feed sends the
// renderer SIGSTOP; SIGCONT when the prompt goes away). Savers animate from
// CLOCK_MONOTONIC, so without help they would jump forward by however long the
// prompt was up. Every hack binary is linked with -Wl,--wrap=clock_gettime: the
// monotonic clocks seen by the saver have the suspended time taken out, so it
// continues from the frame it stopped on. Only the off-screen frame-source mode
// arms this (ncz_time_offset_arm()); on screen the clock passes through.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ncz_time_offset.h"

#include <signal.h>
#include <string.h>
#include <time.h>

int __real_clock_gettime(clockid_t clk, struct timespec *ts);

static volatile sig_atomic_t g_continued;
static int g_armed;
static long long g_offset_ns;          /* suspended time removed so far */
static long long g_last_real_ns;       /* real monotonic time of the last frame mark */
static long long g_last_budget_ns;     /* active feed frame budget the saver publishes */
static long long g_prev_budget_ns;     /* budget in effect just before the last change */
static int g_has_prev_budget;          /* false until the first frame() publishes; gates the change flag */
static int g_budget_changed;           /* 1 on the call after a budget change, else 0 */

static long long ts_ns(const struct timespec *t) { return (long long)t->tv_sec * 1000000000LL + t->tv_nsec; }

static void on_cont(int sig) { (void)sig; g_continued = 1; }

void ncz_time_offset_arm(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_cont;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCONT, &sa, NULL);
    struct timespec t;
    __real_clock_gettime(CLOCK_MONOTONIC, &t);
    g_last_real_ns = ts_ns(&t);
    g_last_budget_ns = 0;
    g_prev_budget_ns = 0;
    g_has_prev_budget = 0;
    g_budget_changed = 0;
    g_armed = 1;
}

void ncz_time_offset_frame(long long budget_ns)
{
    if (!g_armed)
        return;
    struct timespec t;
    __real_clock_gettime(CLOCK_MONOTONIC, &t);
    long long now = ts_ns(&t);
    if (g_continued) {
        /* Everything beyond one frame period since the last frame was spent
         * suspended. */
        long long gap = now - g_last_real_ns - budget_ns;
        if (gap > 0)
            g_offset_ns += gap;
        g_continued = 0;
    }
    /* A change in the active frame budget (warm-15 <-> capped-30 <-> 60)
     * means the controller's old miss/near ring and fail-level history were
     * computed against the wrong target. Flag exactly one transition so the
     * caller can rebase without itself tracking the previous value. The
     * first publish after arm() has no previous budget, so it is NEVER a
     * transition - the public contract is "change AFTER a previous budget".
     * g_prev_budget_ns is the budget the saver was pacing at when the
     * interval that just ended was drawn; it belongs to the previous period. */
    if (g_has_prev_budget && budget_ns != g_last_budget_ns) {
        g_prev_budget_ns = g_last_budget_ns;
        g_budget_changed = 1;
        g_last_budget_ns = budget_ns;
    } else if (!g_has_prev_budget) {
        g_last_budget_ns = budget_ns;
        g_has_prev_budget = 1;
    }
    g_last_real_ns = now;
}

long long ncz_time_offset_budget_ns(void)
{
    return g_armed ? g_last_budget_ns : 0;
}

/* Budget in effect immediately before the last change flagged by
 * ncz_time_offset_budget_changed(). The dt of the frame coinciding with the
 * change was drawn at that rate, so the controller classifies it against
 * g_prev_budget_ns instead of the new budget. 0 before the first change
 * (or when the only publish so far was the first one). */
long long ncz_time_offset_prev_budget_ns(void)
{
    return g_armed ? g_prev_budget_ns : 0;
}

int ncz_time_offset_budget_changed(void)
{
    if (!g_armed) return 0;
    int c = g_budget_changed;
    g_budget_changed = 0;
    return c;
}

int __wrap_clock_gettime(clockid_t clk, struct timespec *ts)
{
    int rc = __real_clock_gettime(clk, ts);
    if (rc != 0 || !g_armed || g_offset_ns == 0)
        return rc;
    if (clk == CLOCK_MONOTONIC || clk == CLOCK_MONOTONIC_RAW || clk == CLOCK_MONOTONIC_COARSE
        || clk == CLOCK_BOOTTIME) {
        long long v = ts_ns(ts) - g_offset_ns;
        ts->tv_sec = (time_t)(v / 1000000000LL);
        ts->tv_nsec = (long)(v % 1000000000LL);
    }
    return rc;
}
