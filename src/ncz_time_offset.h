// SPDX-License-Identifier: Apache-2.0
#ifndef NCZ_TIME_OFFSET_H
#define NCZ_TIME_OFFSET_H

/* Off-screen frame-source mode only: take time spent suspended (SIGSTOP ...
 * SIGCONT) out of the monotonic clocks the saver sees. See ncz_time_offset.c. */
void ncz_time_offset_arm(void);
/* Call once per frame, before drawing; budget_ns is one frame period. */
void ncz_time_offset_frame(long long budget_ns);

/* Last frame budget the renderer published through ncz_time_offset_frame(),
 * in nanoseconds. 0 when ncz_time_offset_arm() has not been called yet
 * (on-screen run, standalone / harness mode, capture-only path). The BH
 * saver's adaptive controller uses this to compare inter-frame dt against
 * the ACTIVE feed cadence (warm-15 / capped-30 / capped-60) instead of
 * the panel refresh. */
long long ncz_time_offset_budget_ns(void);

/* Budget in effect immediately before the most recent budget change
 * (returns 0 if no change has been observed yet, including before the
 * first ncz_time_offset_frame() call after arm()). Lets the controller
 * classify the dt of the frame that ends at the change against the
 * PREVIOUS period (the saver was pacing at that rate when the interval
 * began) rather than the new one. */
long long ncz_time_offset_prev_budget_ns(void);

/* True for exactly one call after ncz_time_offset_frame() observes a
 * different budget than the previous frame, and 0 on subsequent calls until
 * the next change. The FIRST ncz_time_offset_frame() after arm() is
 * NEVER a transition (there is no previous budget to differ from).
 * Lets callers clear stale miss/near windows without having to track
 * the previous budget themselves. Unarmed (before arm()) never reports
 * a change. */
int ncz_time_offset_budget_changed(void);

#endif
