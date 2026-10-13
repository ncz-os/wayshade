/* SPDX-License-Identifier: Apache-2.0 */
/* ncz_platform.c — wayland-backend implementation of ncz_platform.h.
 *
 * For now this is the only backend. Adding macos.m or win32.c later
 * is a search-and-replace of the platform surfaces (monotonic clock,
 * entropy, install path resolution). Hacks include only
 * ncz_platform.h -- never this .c.
 *
 * Time is monotonic via clock_gettime(CLOCK_MONOTONIC). Entropy
 * comes from /dev/urandom with a CLOCK_REALTIME+getpid fallback for
 * the corner case where /dev/urandom is unreadable (containers, etc).
 * Asset resolution hard-codes /usr/share/ncz-screensavers/shaders/
 * for now -- the only place the install dir leaks into hack code.
 * Dev builds fall back to vendor/<hack>/ under cwd via
 * ncz_asset_path fallback below (small, in this file -- the runtime
 * lookup is the primary answer).
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ncz_platform.h"
#include "gles3_harness_hooks.h"

/* -------------------------------------------------------------------- */
/* ncz_now                                                                  */
/* -------------------------------------------------------------------- */
double ncz_now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) == 0)
        return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
    return 0.0;
}

/* -------------------------------------------------------------------- */
/* ncz_seed                                                                 */
/* -------------------------------------------------------------------- */
uint32_t ncz_seed(void) {
    uint32_t s = 0;
    int f = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (f >= 0) {
        ssize_t n = read(f, &s, sizeof s);
        close(f);
        if (n == (ssize_t)sizeof s && s != 0) return s;
    }
    struct timespec t;
    if (clock_gettime(CLOCK_REALTIME, &t) == 0)
        s = (uint32_t)(t.tv_nsec ^ t.tv_sec ^ (uint32_t)getpid());
    if (s == 0) s = 1;
    return s;
}

/* -------------------------------------------------------------------- */
/* ncz_asset_path                                                           */
/* -------------------------------------------------------------------- */
/* Compose an absolute installed-asset path. The base directory can be
 * overridden with NCZ_ASSET_DIR for tests; the default is the
 * production install dir.
 * Returns 1 if the buffer was filled, 0 if too small. */
int ncz_asset_path(const char *basename, char *out, int cap) {
    if (!basename || !out || cap <= 0) return 0;
    const char *base = getenv("NCZ_ASSET_DIR");
    if (!base || !*base) base = "/usr/share/ncz-screensavers/shaders";
    int n = snprintf(out, (size_t)cap, "%s/%s", base, basename);
    return (n > 0 && n < cap) ? 1 : 0;
}

/* -------------------------------------------------------------------- */
/* ncz_frame_size, ncz_swap                                                 */
/* -------------------------------------------------------------------- */
/* The wayland harness owns the framebuffer; expose what its draw_cb's
 * have already captured. The gles3_harness.c sees the X11 ModeInfo
 * from xscreensaver_compat, which carries xgwa.width/height and an
 * egl_display / egl_surface pair.
 *
 * For now we read environment-supplied values, refreshed once per
 * draw via ncz_harness_attach_frame_size() in the harness. The hack
 * never sees the surface handle directly. */
static int g_fb_w = 0;
static int g_fb_h = 0;

void ncz_harness_attach_frame_size(int w, int h) {
    if (w > 0) g_fb_w = w;
    if (h > 0) g_fb_h = h;
}

void ncz_frame_size(int *w, int *h) {
    if (w) *w = g_fb_w;
    if (h) *h = g_fb_h;
}

/* -------------------------------------------------------------------- */
/* ncz_log_diag                                                             */
/* -------------------------------------------------------------------- */
#include <stdarg.h>
void ncz_log_diag(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[diag] ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);
}

/* -------------------------------------------------------------------- */
/* ncz_env_read_int                                                        */
/* -------------------------------------------------------------------- */
/* Tiny line scanner for KEY=VALUE pairs. Called once per read on a
 * file that is at most a handful of lines; performance is fine.
 * Looks in (in order):
 *   $XDG_DATA_HOME/ncz-screensavers/sensors.env
 *   $HOME/.local/share/ncz-screensavers/sensors.env
 *   /etc/ncz-screensavers/sensors.env
 *   /run/ncz-screensavers/sensors.env
 *
 * Format: KEY=VALUE per line, blank lines and lines starting with '#'
 * ignored. The first matching key wins. */
static const char *kEnvRoots[] = {
    "/etc/ncz-screensavers/sensors.env",
    "/run/ncz-screensavers/sensors.env",
};
static char g_user_env_buf[1024];
static const char *user_env_path(void) {
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg) {
        snprintf(g_user_env_buf, sizeof g_user_env_buf,
                 "%s/ncz-screensavers/sensors.env", xdg);
        return g_user_env_buf;
    }
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(g_user_env_buf, sizeof g_user_env_buf,
                 "%s/.local/share/ncz-screensavers/sensors.env", home);
        return g_user_env_buf;
    }
    return NULL;
}
static int scan_file(const char *path, const char *key, int *out_v) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *end = eq - 1;
        while (end > line && (*end == ' ' || *end == '\t')) end--;
        *(end + 1) = 0;
        if (strcmp(line, key) != 0) continue;
        *out_v = atoi(eq + 1);
        found = 1;
        break;
    }
    fclose(f);
    return found;
}
int ncz_env_read_int(const char *key, int *out_v) {
    if (!key || !out_v) return 0;
    int v;
    const char *up = user_env_path();
    if (up && scan_file(up, key, &v)) { *out_v = v; return 1; }
    for (size_t i = 0; i < sizeof(kEnvRoots)/sizeof(kEnvRoots[0]); i++) {
        if (scan_file(kEnvRoots[i], key, &v)) { *out_v = v; return 1; }
    }
    return 0;
}
