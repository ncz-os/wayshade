/* ncz_platform.h — minimal platform abstraction for ncz-screensavers.
 *
 * Per the platform abstraction rule (docs/superpowers/specs/
 * 2026-09-25-platform-abstraction-rule.md), hack code is pure C plus
 * GLSL ES 3.00 and never touches platform headers, POSIX-only time/
 * process/IO or filesystem directly. Everything platform-specific
 * sits behind this interface, with one backend per target
 * platform (wayland, win32, macos, ...).
 *
 * The interface is intentionally small: only what the hacks actually
 * use today. Add to it as a real need appears; do not pre-empt.
 *
 *   ncz_now()             monotonic seconds as double
 *   ncz_seed()            entropy for per-launch randomisation
 *   ncz_asset_path()      resolve an installed asset (shader, data)
 *                         by name into the backends's install dir
 *   ncz_frame_size()      current drawable size in pixels
 *   ncz_log_diag()        print a structured [diag] log line
 *
 * Lifetime and event loop live in the backend; hacks render frames
 * via the xscreensaver-compat draw_cb. See src/gles3_harness.c for
 * the wayland-backend wiring of these primitives.
 *
 * NOTE on ncz_swap: presentation is owned by the harness (it knows
 * the EGL surface and Wayland frame-callback timing). Adding
 * ncz_swap() is a planned follow-up; for now hacks must not present
 * directly -- the harness does it after draw_cb returns.
 */
#ifndef NCZ_PLATFORM_H
#define NCZ_PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Monotonic clock in seconds (double). Replaces clock_gettime. */
double ncz_now(void);

/* 32 bits of entropy. Replaces open(/dev/urandom) + read + close.
 * Returns non-zero. */
uint32_t ncz_seed(void);

/* Fill `out` (size `cap`, including NUL) with the absolute path of
 * the installed asset named `basename` (e.g. "lines.frag"). The
 * backend chooses the install dir (e.g. /usr/share/ncz-screensavers/
 * shaders/ on Linux). Returns 1 if a path was produced, 0 if the
 * buffer is too small. The cwd-relative fallback in callers is
 * fine for dev builds but should not be the only path on production
 * machines. */
int ncz_asset_path(const char *basename, char *out, int cap);

/* Current drawable / framebuffer size in pixels. The wayland
 * backend sizes this from the harness's last measured frame. */
void ncz_frame_size(int *w, int *h);

/* Print a single line to stderr, prefixed [diag] (for the harness
 * log format). \n appended automatically. */
void ncz_log_diag(const char *fmt, ...);

/* Read an integer from a key=value cache file. The hack never opens
 * /proc or /sys directly; instead a separate poller writes
 * sensors.env (or the per-platform equivalent) to a known location
 * and the hack consumes it through this primitive.
 *
 * Looked up in:
 *   $XDG_DATA_HOME/ncz-screensavers/sensors.env (or ~/.local/share/...)
 *   /etc/ncz-screensavers/sensors.env
 *   /run/ncz-screensavers/sensors.env
 *
 * Returns 1 if the key was found (out_v filled), 0 if not. Missing
 * file is the same code path as zero — callers should treat absence
 * as neutral bias, not an error. */
int ncz_env_read_int(const char *key, int *out_v);

#ifdef __cplusplus
}
#endif

#endif /* NCZ_PLATFORM_H */
