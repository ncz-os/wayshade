/* SPDX-License-Identifier: Apache-2.0 */
/* ncz_feed_render.h - off-screen render mode for a GLES3 hack, used by the
 * out-of-process frame source (ncz-screensaver-feed).
 *
 * A screensaver hack is normally driven by gles3_harness.c, which owns a
 * Wayland layer-shell surface. While a session is locked, ext-session-lock-v1
 * presents only the lock client's surface, so a separate hack process cannot
 * draw there. Instead the lock client imports a buffer from this process over a
 * Unix socket (lock_framesrc_proto.h). This entry point runs the SAME hack
 * init/draw callbacks that gles3_harness.c runs, but into an off-screen
 * framebuffer with no compositor at all, and streams the framebuffer to the
 * feed as wl_shm (memfd) frames.
 *
 * Nothing about the hack changes: HACK_TABLE, the shim, the option parser and
 * the render-scale logic are all reused. Only the surface/swap path is replaced
 * by an EGL surfaceless (or device) context and an FBO.
 *
 * Enabled per-process by NCZ_FEED_SOCKET. gles3_harness.c calls this before it
 * connects to any Wayland display, so a normal run is completely unaffected.
 */
#ifndef NCZ_FEED_RENDER_H
#define NCZ_FEED_RENDER_H

struct xscreensaver_function_table;

/* Run the hack in off-screen frame-source mode. `hack` is the process's
 * HACK_TABLE. Returns the process exit code; never returns to the caller's
 * normal Wayland path (the harness returns this directly). */
int ncz_feed_render_main(struct xscreensaver_function_table *hack,
                         int argc, char **argv);

/* Cadence helper exposed for unit tests: compute the effective per-frame
 * rate from the configured fps, warm-band fps, and current thermal level.
 * Always returns >= 1 so the cadence budget 1.0 / fps_eff never divides by
 * zero. `level` is 0 (normal), 1 (warm), or 2 (hot/paused). */
int ncz_feed_effective_fps(int fps, int warm_fps, int level);

/* Adaptive-quality predicate shared with the BH saver's bh_pace controller.
 *
 * Classifies a single frame's inter-draw dt against the ACTIVE feed
 * cadence, not just the panel refresh. The historical bug: a saver that is
 * intentionally capped at 30 fps sees a 33.33 ms inter-frame interval; if
 * the panel refresh is 16.67 ms (60 Hz) the controller used to mark 100 % of
 * those frames as "missed display frames" and stepped ray quality all the
 * way down to the floor.
 *
 *   target_ms = (effective_budget_ns > 0)
 *                 ? max(panel_period_ms, effective_budget_ns / 1e6)
 *                 : panel_period_ms;         (standalone: panel only)
 *
 * A frame whose dt exceeds target_ms * 1.30 is a miss (out_miss = 1); a
 * frame whose dt exceeds target_ms * 1.08 is "near" (out_near = 1). Both
 * are 0 otherwise. The call is pure and stateless; the controller owns the
 * 60-frame ring history. */
void ncz_feed_adapt_classify(double dt_ms, double panel_period_ms,
                             long long effective_budget_ns,
                             int *out_miss, int *out_near);

#endif /* NCZ_FEED_RENDER_H */
