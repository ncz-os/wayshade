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

#endif /* NCZ_FEED_RENDER_H */
