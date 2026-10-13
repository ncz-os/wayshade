/* SPDX-License-Identifier: Apache-2.0 */
/* _POSIX_C_SOURCE for sigaction + clock_gettime. */
#define _POSIX_C_SOURCE 200809L

/*
 * gles3_harness.c — driver binary for a vendored xscreensaver hack
 *                   running on a Wayland/EGL/GLES3 surface.
 *
 * The hack talks to libGLESv2 directly: no desktop GL, no GLX
 * passthrough, no fixed-function emulation. The shared headers
 * (ncz_gl.h, ncz_hack_shim.h, xscreensaver_compat.h) supply the
 * ModeInfo/ncz_* plumbing every binary shares.
 *
 * Pipeline:
 *
 *   ┌───────────────────────────────────────────────┐
 *   │ gles3_harness.c (this file)                   │
 *   │  - Owns Wayland + EGL + frame loop            │
 *   │  - Calls init_<hack> once                     │
 *   │  - Calls draw_<hack> each frame               │
 *   │  - Calls ncz_gles3_runtime_init() at startup  │
 *   └──────────────┬────────────────────────────────┘
 *                  │
 *                  ▼
 *   ┌───────────────────────────────────────────────┐
 *   │ src/ncz_gl.{h,c} + ncz_hack_shim.{h,c}        │
 *   │  - EGL/GLES3 headers and ModeInfo shims       │
 *   │  - Shader (vertex+fragment, lit, textured)    │
 *   │  - ncz_im_* immediate-mode helpers            │
 *   │  - ncz_mat_stack_* matrix stack               │
 *   │  - nczGLList_* VBO-based gllist upload        │
 *   │  - ncz_dl_* display-list recorder             │
 *   └──────────────┬────────────────────────────────┘
 *                  │
 *                  ▼
 *   ┌───────────────────────────────────────────────┐
 *   │ libGLESv2.so → Mesa panfrost / Mali blob      │
 *   │  - GLES 3.2 native                            │
 *   │  - Mali-G720 GLES 3.2 driver                  │
 *   │  - Outputs to our Wayland wl_egl_window       │
 *   └───────────────────────────────────────────────┘
 *
 * Build: linked against system libwayland-client, libwayland-egl,
 *        libEGL, libGLESv2. Nothing else.
 *
 * Run:   ./boing_gles3
 *
 * Expected on real hardware: rotating "Boing" ball in a colored
 * grid cage, like the 1984 Amiga demo. Quits on any key press.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <poll.h>
#include <math.h>
#include <sys/stat.h>

#include <wayland-client.h>
#include <wayland-egl.h>

#include <EGL/egl.h>
#include <EGL/eglplatform.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>

#include "ncz_gl.h"

/* sRGB write control is an ES extension (EXT_sRGB_write_control); the desktop
 * token is the same value.  Only touch it where the driver exposes it, so the
 * calls cannot raise GL_INVALID_ENUM. */
#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif
static int srgb_control_available(void) {
    static int v = -1;
    if (v < 0) {
        const char *e = (const char *)glGetString(GL_EXTENSIONS);
        v = (e && strstr(e, "GL_EXT_sRGB_write_control")) ? 1 : 0;
    }
    return v;
}
#include "ncz_hack_shim.h"
#include "ncz_platform.h"
#include "gles3_harness_hooks.h"
#include "gles3_harness_png.h"
#include "ncz_harness_cfg.h"
#include "ncz_render.h"
#include "ncz_gpu_guard.h"
#include "ncz_stats.h"
#include "ncz_feed_render.h"

#include "wayland-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

/* The vendored hack emits this via its own registration macro
 * (XSCREENSAVER_MODULE_2 or similar). For our pilots we point at
 * the GLES3-native port directly:
 *   - boing_gles3  -> gles3_boing_xscreensaver_function_table
 *   - companion_gles3 -> gles3_companioncube_xscreensaver_function_table
 *
 * The hack's table is what the harness drives via init_cb / draw_cb.
 * HACK_TABLE is set by meson per target. */
#ifndef HACK_TABLE
#define HACK_TABLE gles3boing_xscreensaver_function_table
#endif
extern struct xscreensaver_function_table HACK_TABLE;
static struct xscreensaver_function_table *hack = &HACK_TABLE;

/* ----------------------------------------------------------------------- */
/* Macros                                                                  */
/* ----------------------------------------------------------------------- */

#define DIE(call, expected, fmt, ...)                                      \
    do {                                                                   \
        long long _ret = (long long)(call);                                \
        long long _exp = (long long)(expected);                            \
        if (_ret != _exp) {                                                \
            fprintf(stderr,                                               \
                    "gles3_harness: %s failed at %s:%d: " fmt "\n",       \
                    #call, __FILE__, __LINE__, ##__VA_ARGS__);             \
            ncz_harness_die(1);                                                       \
        }                                                                  \
    } while (0)

/* ----------------------------------------------------------------------- */
/* Application state                                                       */
/* ----------------------------------------------------------------------- */

struct app {
    struct wl_display              *display;
    struct wl_registry             *registry;
    struct wl_compositor           *compositor;
    struct wl_seat                 *seat;
    struct wl_keyboard             *keyboard;
    struct wl_output               *output;
    struct zwlr_layer_shell_v1     *layer_shell;
    struct xdg_wm_base             *wm_base;
    struct xdg_surface             *xdg_surface;
    struct xdg_toplevel            *xdg_toplevel;
    int                             pending_w, pending_h;
    struct wl_surface              *surface;
    struct zwlr_layer_surface_v1   *layer_surface;

    struct wl_egl_window *egl_window;
    EGLDisplay  egl_display;
    EGLConfig   egl_config;
    EGLSurface  egl_surface;
    EGLContext  egl_context;

    int width, height;            /* render (buffer) size */
    int nat_w, nat_h;             /* surface (native) size */
    double rscale, last_adapt_s;
    struct wp_viewporter *viewporter;
    struct wp_viewport *viewport;
    struct wl_callback *frame_cb;
    bool frame_in_flight;
    bool configured;

    volatile sig_atomic_t running;

    /* The ModeInfo we own — passed to init_<hack> / draw_<hack> / etc. */
    ModeInfo mi;
};

static struct app *g_app = NULL;

static void on_signal(int sig) {
    (void)sig;
    if (g_app) g_app->running = 0;
}

/* ----------------------------------------------------------------------- */
/* Wayland registry / seat / keyboard                                      */
/* ----------------------------------------------------------------------- */

static void wm_base_ping(void *d, struct xdg_wm_base *b, uint32_t serial) {
    (void)d;
    xdg_wm_base_pong(b, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

static void reg_global(void *d, struct wl_registry *r, uint32_t n,
                       const char *iface, uint32_t v) {
    (void)v;
    struct app *a = d;
    if (strcmp(iface, wl_compositor_interface.name) == 0)
        a->compositor = wl_registry_bind(r, n, &wl_compositor_interface, 4);
    else if (strcmp(iface, wl_seat_interface.name) == 0)
        a->seat = wl_registry_bind(r, n, &wl_seat_interface, 7);
    else if (strcmp(iface, wl_output_interface.name) == 0 && !a->output)
        a->output = wl_registry_bind(r, n, &wl_output_interface, 4);
    else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        if (!getenv("NCZ_NO_LAYER_SHELL"))
            a->layer_shell = wl_registry_bind(r, n,
                                              &zwlr_layer_shell_v1_interface, 4);
    }
    else if (strcmp(iface, wp_viewporter_interface.name) == 0)
        a->viewporter = wl_registry_bind(r, n, &wp_viewporter_interface, 1);
    else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
        a->wm_base = wl_registry_bind(r, n, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(a->wm_base, &wm_base_listener, a);
    }
}

static void reg_global_remove(void *d, struct wl_registry *r, uint32_t n) {
    (void)d; (void)r; (void)n;
}

static const struct wl_registry_listener reg_listener = {
    .global = reg_global, .global_remove = reg_global_remove,
};

static void keyh_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t,
                     uint32_t key, uint32_t state) {
    (void)k; (void)s; (void)t; (void)key;
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        struct app *a = d;
        a->running = 0;
    }
}
static void keyh_enter(void *d, struct wl_keyboard *k, uint32_t s,
                       struct wl_surface *sf, struct wl_array *keys) {
    (void)d; (void)k; (void)s; (void)sf; (void)keys;
}
static void keyh_leave(void *d, struct wl_keyboard *k, uint32_t s,
                       struct wl_surface *sf) {
    (void)d; (void)k; (void)s; (void)sf;
}
static void keyh_keymap(void *d, struct wl_keyboard *k, uint32_t f, int fd,
                        uint32_t sz) {
    (void)d; (void)k; (void)f; (void)fd; (void)sz;
}
static void keyh_modifiers(void *d, struct wl_keyboard *k, uint32_t s,
                           uint32_t md, uint32_t ml, uint32_t lo,
                           uint32_t g) {
    (void)d; (void)k; (void)s; (void)md; (void)ml; (void)lo; (void)g;
}
static void keyh_repeat(void *d, struct wl_keyboard *k, int rate, int delay) {
    (void)d; (void)k; (void)rate; (void)delay;
}
static const struct wl_keyboard_listener kbd_listener = {
    .key = keyh_key, .enter = keyh_enter, .leave = keyh_leave,
    .keymap = keyh_keymap, .modifiers = keyh_modifiers,
    .repeat_info = keyh_repeat,
};

static void seat_caps(void *d, struct wl_seat *s, enum wl_seat_capability c) {
    (void)s;
    struct app *a = d;
    if ((c & WL_SEAT_CAPABILITY_KEYBOARD) && !a->keyboard) {
        a->keyboard = wl_seat_get_keyboard(a->seat);
        if (a->keyboard)
            wl_keyboard_add_listener(a->keyboard, &kbd_listener, a);
    }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) {
    (void)d; (void)s; (void)n;
}
static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_caps, .name = seat_name,
};

/* ----------------------------------------------------------------------- */
/* Layer surface                                                           */
/* ----------------------------------------------------------------------- */

/* ----------------------------------------------------------------------- */
/* Render resolution                                                       */
/* ----------------------------------------------------------------------- */

static double mono_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

/* GL_RENDERER captured while the startup context is current (the platform
 * render-size caps depend on it). */
static char g_renderer[512];

/* True when EGL_KHR_gl_colorspace was exposed at init time.  We then ask
 * every window surface for EGL_GL_COLORSPACE_LINEAR so the driver does not
 * sRGB-encode the shaders' display-referred output a second time (see
 * ncz_gl.h and the frame-report comment in report_framebuffer()). */
static bool g_gl_colorspace;

/* Opaque region covers the whole (native) surface; re-issued on every resize. */
static void set_opaque_region(struct app *a) {
    struct wl_region *opaque = wl_compositor_create_region(a->compositor);
    if (!opaque) return;
    wl_region_add(opaque, 0, 0, a->nat_w, a->nat_h);
    wl_surface_set_opaque_region(a->surface, opaque);
    wl_region_destroy(opaque);
}

/* Decide the buffer size for a surface of nw x nh (see ncz_render.h). The
 * buffer is smaller than the surface when scaled; wp_viewport upscales it in
 * the compositor (no extra GPU pass). Without wp_viewporter, or when nothing
 * needs reducing, the buffer stays at native size. */
static void apply_render_size(struct app *a, int nw, int nh) {
    int expl = 0;
    int cap = ncz_cfg_max_render_height(&expl);
    const char *rend = g_renderer[0] ? g_renderer : (const char *)glGetString(GL_RENDERER);
    /* Test only: NCZ_TEST_SURFACE_SIZE=WxH makes the size/cap/scale logic act as if the surface
     * were WxH (to measure a 4K operating point on a smaller panel). The viewport destination
     * stays the real surface size. */
    int vw = nw, vh = nh, virt = 0;
    const char *tss = getenv("NCZ_TEST_SURFACE_SIZE");
    if (tss && sscanf(tss, "%dx%d", &vw, &vh) == 2 && vw >= 2 && vh >= 2) virt = 1;
    else { vw = nw; vh = nh; }
    if (!expl) cap = ncz_render_platform_cap(rend, vh);
    int rw = vw, rh = vh;
    int scaled = ncz_render_size(vw, vh, a->rscale, cap, &rw, &rh);
    if (virt && (rw != nw || rh != nh)) scaled = 1;
    if (scaled && !a->viewporter) {
        fprintf(stderr, "[diag] gles3_harness: wp_viewporter unavailable; rendering at native %dx%d\n", nw, nh);
        scaled = 0; rw = nw; rh = nh;
    }
    if (scaled) {
        if (!a->viewport)
            a->viewport = wp_viewporter_get_viewport(a->viewporter, a->surface);
        wp_viewport_set_destination(a->viewport, nw, nh);
    } else if (a->viewport) {
        wp_viewport_set_destination(a->viewport, nw, nh);
    }
    if (rw != a->width || rh != a->height || a->nat_w != nw || a->nat_h != nh)
        fprintf(stderr, "[diag] gles3_harness: render size %dx%d (surface %dx%d, scale %.2f, cap %d, %s)\n",
                rw, rh, nw, nh, a->rscale, cap, scaled ? "wp_viewport upscale" : "native");
    a->nat_w = nw; a->nat_h = nh;
    a->width = rw; a->height = rh;
    if (a->configured) set_opaque_region(a);
}

/* After apply_render_size on a live window: resize the buffer, publish the
 * new size to the hack and tell it to reshape. */
static void commit_render_size(struct app *a) {
    wl_egl_window_resize(a->egl_window, a->width, a->height, 0, 0);
    g_harness_width = a->width;
    g_harness_height = a->height;
    a->mi.xgwa.width = a->width;
    a->mi.xgwa.height = a->height;
    if (hack->reshape_cb) hack->reshape_cb(&a->mi, a->width, a->height);
}

/* Software-renderer guard; see ncz_gpu_guard.h. Called with a context current,
 * both before the surface exists and once it does (some stacks only report
 * GL_RENDERER later). An unknown (NULL) renderer is logged, not trusted. */
static void guard_renderer(const char *where) {
    const char *rend = (const char *)glGetString(GL_RENDERER);
    if (!rend) { fprintf(stderr, "[guard] renderer string unavailable at %s\n", where); return; }
    if (!g_renderer[0] && rend[0]) {
        snprintf(g_renderer, sizeof g_renderer, "%s", rend);
        ncz_render_set_renderer(rend);
        fprintf(stderr, "[diag] gles3_harness: gpu class %s (%s)\n", ncz_gpu_class_current(), rend);
    }
    char gmsg[512];
    /* Only honored when the machine has no hardware GPU (see ncz_gpu_guard.h). */
    const char *allow = getenv("NCZ_ALLOW_SOFTWARE");
    if (!allow || !allow[0]) allow = getenv("NCZ_ALLOW_SOFTWARE_FALLBACK");
    if (ncz_gpu_guard_check("", rend, allow, gmsg, sizeof gmsg)) {
        fprintf(stderr, "[guard] %s\n", gmsg);
        ncz_harness_die(3);
    }
}

/* One-time hack initialisation at a known size: harness globals, declared
 * defaults, init_GL shim, init_cb and the first reshape. Shared by the
 * surface configure path and the headless path (NCZ_HARNESS_HEADLESS). */
static void hack_init_once(struct app *a) {
    g_harness_width = a->width;
    g_harness_height = a->height;
    g_harness_initialized = 1;

    /* Apply the hack's declared defaults BEFORE init.
     *
     * The hack never assigns its tunables itself -- upstream
     * xscreensaver writes them through the ModeSpecVar table during
     * option parsing. We don't parse options, but we must still do
     * that write, or every tunable stays at its BSS default -- which
     * makes parse_color receive NULL color strings and exit.
     *
     * init_GL records the harness's width/height into the ModeInfo
     * so the hack's reshape_*() receives non-zero dimensions. The
     * init_GL here is a thin shim that just sets those fields;
     * we call it for the same effect. */
    xs_compat_apply_var_defaults(hack->opts, hack->defaults_str);
    xs_compat_apply_mode_defaults(&a->mi);
    init_GL(&a->mi);
    hack->init_cb(&a->mi);
    if (hack->reshape_cb) {
        hack->reshape_cb(&a->mi, a->width, a->height);
    }
    fprintf(stderr, "[diag] gles3_harness: init returned\n");
    a->configured = true;
}

static void surface_configured(struct app *a, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return;

    if (!a->configured) {
        a->rscale = ncz_cfg_render_scale();
        /* Weak GPUs (Intel UHD class) start at half resolution unless the user chose a scale. */
        if (!ncz_cfg_render_scale_is_set() && !strcmp(ncz_gpu_class_current(), "weak") &&
            !(ncz_hack_native_scale && ncz_hack_native_scale()))
            a->rscale = 0.5;
        apply_render_size(a, (int)w, (int)h);
        a->egl_window = wl_egl_window_create(a->surface, a->width, a->height);
        if (!a->egl_window) {
            fprintf(stderr, "gles3_harness: wl_egl_window_create failed\n");
            ncz_harness_die(1);
        }
        /* Ask for a LINEAR window surface when the extension allows.
         * ncz_gles3_choose_config() already prefers a linear config, but
         * the surface attribute is the one drivers honour most reliably;
         * requesting both keeps every vendor on the same colour path. */
        EGLint surf_attr[3];
        int sn = 0;
        if (g_gl_colorspace) {
            surf_attr[sn++] = EGL_GL_COLORSPACE;
            surf_attr[sn++] = EGL_GL_COLORSPACE_LINEAR;
        }
        surf_attr[sn] = EGL_NONE;
        a->egl_surface = eglCreateWindowSurface(a->egl_display, a->egl_config,
                                                (EGLNativeWindowType)a->egl_window,
                                                surf_attr);
        if (a->egl_surface == EGL_NO_SURFACE) {
            fprintf(stderr, "gles3_harness: eglCreateWindowSurface failed (0x%x)\n",
                    (unsigned int)eglGetError());
            ncz_harness_die(1);
        }
        DIE(eglMakeCurrent(a->egl_display, a->egl_surface, a->egl_surface,
                           a->egl_context), EGL_TRUE, "eglMakeCurrent failed");
        /* Reassert the spec default on the live surface: GLES3 says
         * GL_FRAMEBUFFER_SRGB is DISABLED, and we never want the driver
         * to sRGB-encode the shader output.  Some drivers re-derive the
         * toggle from the surface tag when the surface is first bound,
         * so the startup disable is not enough on its own. */
        if (srgb_control_available())
            glDisable(GL_FRAMEBUFFER_SRGB);
        guard_renderer("surface");

        /* Explicitly request vblank-paced eglSwapBuffers. Measured
         * 2026-09-22 on all three platforms (Orion O6N/Mali, Navi14/radeonsi,
         * Intel UHD 630/iris): with or without this call, boing_gles3 produced
         * exactly 120 frames in 3 seconds (~40 fps), so the Mesa/CIX
         * Wayland winsys appears to already pace via its own internal
         * wl_surface.frame. But making the request explicit is the
         * correct shape for an EGL application and protects against
         * future winsys changes or vendors that don't self-pace. */
        eglSwapInterval(a->egl_display, 1);

        /* Set up the opaque region: a transparent surface blends
         * against the desktop, which is invisible. */
        struct wl_region *opaque = wl_compositor_create_region(a->compositor);
        if (opaque) {
            wl_region_add(opaque, 0, 0, a->nat_w, a->nat_h);
            wl_surface_set_opaque_region(a->surface, opaque);
            wl_region_destroy(opaque);
        }

        fprintf(stderr, "[diag] gles3_harness: calling init...\n");
        /* Set the harness globals so init_GL() (still called for
         * shape compatibility) records the configured size into
         * mi->xgwa.width/height. Without this, the hack's reshape_*()
         * runs with width=height=0 and computes NaN aspect ratios. */
        hack_init_once(a);
    } else if (w != (uint32_t)a->nat_w || h != (uint32_t)a->nat_h) {
        apply_render_size(a, (int)w, (int)h);
        commit_render_size(a);
    }
}

static void ls_configure(void *d, struct zwlr_layer_surface_v1 *ls,
                         uint32_t serial, uint32_t w, uint32_t h) {
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    surface_configured((struct app *)d, w, h);
}

static void ls_closed(void *d, struct zwlr_layer_surface_v1 *ls) {
    (void)ls; struct app *a = d; a->running = 0;
}

static const struct zwlr_layer_surface_v1_listener ls_listener = {
    .configure = ls_configure, .closed = ls_closed,
};

/* ----------------------------------------------------------------------- */
/* xdg-shell fallback                                                      */
/* ----------------------------------------------------------------------- */

static void xdg_surf_configure(void *d, struct xdg_surface *xs,
                               uint32_t serial) {
    struct app *a = d;
    xdg_surface_ack_configure(xs, serial);
    surface_configured(a, a->pending_w ? (uint32_t)a->pending_w : 1920,
                          a->pending_h ? (uint32_t)a->pending_h : 1080);
}
static const struct xdg_surface_listener xdg_surf_listener = {
    .configure = xdg_surf_configure,
};

static void xdg_top_configure(void *d, struct xdg_toplevel *t,
                              int32_t w, int32_t h, struct wl_array *states) {
    (void)t; (void)states;
    struct app *a = d;
    if (w > 0 && h > 0) { a->pending_w = w; a->pending_h = h; }
}
static void xdg_top_close(void *d, struct xdg_toplevel *t) {
    (void)t; ((struct app *)d)->running = 0;
}
static const struct xdg_toplevel_listener xdg_top_listener = {
    .configure = xdg_top_configure, .close = xdg_top_close,
};

/* ----------------------------------------------------------------------- */
/* Frame callback                                                          */
/* ----------------------------------------------------------------------- */

/*
 * Root cause of the AMD64/Mesa (radeonsi, Intel iGPU) animation stall,
 * measured 2026-09-22 via WAYLAND_DEBUG=1 on a Radeon Pro 5500M laptop: the old code here
 * called wl_surface_frame() itself AND used eglSwapBuffers() through
 * Mesa's EGL-Wayland platform, which ALSO issues its own internal
 * wl_surface.frame request as part of normal buffer presentation. Two
 * frame-callback requests landed on the same surface a few microseconds
 * apart (mesa egl surface queue's vs this file's own Default Queue one);
 * a surface only gets ONE frame callback fulfilled per commit, so this
 * file's own explicit callback never received its .done event and the
 * loop below never woke up again after the first frame. Panthor/Mesa on
 * Orion O6N happened not to hit this (different winsys timing), which is why
 * it worked there and nowhere else.
 *
 * Fix: don't manage a manual Wayland frame callback on an EGL-owned
 * window surface at all. EGL's own Wayland winsys already paces
 * presentation inside eglSwapBuffers; drive the loop by dispatching
 * pending Wayland events (non-blocking) and drawing+swapping every
 * iteration, the same shape as any other GL-on-EGL app.
 */
static void report_framebuffer(struct app *a, unsigned long frame) {
    size_t npixels = (size_t)a->width * (size_t)a->height;
    unsigned char *pixels = malloc(npixels * 4);
    if (!pixels) {
        fprintf(stderr, "gles3_harness: framebuffer sample allocation failed\n");
        a->running = 0;
        return;
    }

    glReadPixels(0, 0, a->width, a->height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    GLenum error = glGetError();
    size_t nonblack = 0;
    /* Alpha is read back and recorded (the PNG/raw dump already carries it):
     * a shader that leaves .a at 0 makes a layer-shell surface composite to
     * nothing even when its RGB is correct, so the mean alpha is a first-class
     * diagnostic. */
    uint64_t alpha_sum = 0;
    /* Histogram of the darkest region. Bucket each RGB sample into one
     * of 16 bins (0..15, 16..31, ..., 240..255) — coarse enough to be
     * readable, fine enough to discriminate a "dither floor" (1..2)
     * from an "sRGB-encoded floor" (10..30). */
    size_t hist[16] = {0};
    /* Per-channel minimums over the darkest quarter of the frame
     * (lower-left quadrant of the readback — origin (0,0) bottom-left
     * in GL, so visually top-left after the dump's flip). The event
     * horizon (the wash that's bright on NVIDIA, true black on AMD)
     * typically lands in the upper-left of the rendered blackhole, so
     * sample that quadrant specifically. */
    unsigned int min_dark_r = 255, min_dark_g = 255, min_dark_b = 255;
    unsigned int dark_count = 0;
    /* p1 / p5 percentiles over the whole frame, sorted via histogram. */
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < npixels; i++) {
        const unsigned char *p = pixels + i * 4;
        if (p[0] || p[1] || p[2]) nonblack++;
        alpha_sum += p[3];
        for (int c = 0; c < 3; c++) {
            unsigned int v = (unsigned int)p[c];
            if (v < 256) {
                size_t b = (v >> 4) & 0xF;
                hist[b]++;
            }
            hash ^= p[c];
            hash *= UINT64_C(1099511628211);
        }
        /* Sample the darkest quarter: pick pixels whose geometric
         * position is the upper-left quadrant (top of the rendered
         * image; since glReadPixels returns rows bottom-to-top, this
         * corresponds to the upper half of `pixels`). */
        size_t y = i / (size_t)a->width;
        if (y < (size_t)a->height / 2 && (i % (size_t)a->width) < (size_t)a->width / 2) {
            dark_count++;
            if (p[0] < min_dark_r) min_dark_r = p[0];
            if (p[1] < min_dark_g) min_dark_g = p[1];
            if (p[2] < min_dark_b) min_dark_b = p[2];
        }
    }

    /* Compute p1 and p5 percentiles of the whole-frame distribution
     * from the histogram: the rank we want is npixels/100 and
     * 5*npixels/100. */
    size_t target_p1 = npixels / 100;
    size_t target_p5 = (npixels * 5) / 100;
    size_t acc = 0;
    unsigned int p1 = 255, p5 = 255;
    for (int b = 0; b < 16; b++) {
        acc += hist[b];
        if (acc >= target_p1 && p1 == 255) p1 = (unsigned int)(b * 16);
        if (acc >= target_p5 && p5 == 255) p5 = (unsigned int)(b * 16);
    }

    /* sRGB write state on the bound context. GL_FRAMEBUFFER_SRGB is a
     * core GLES3 token (no extension needed for the toggle itself;
     * GLES3 inherits the GL_ARB_framebuffer_sRGB behavior). But the
     * default per spec is DISABLED in GLES — so any driver that
     * reports it enabled here has done something beyond spec. Print
     * the live state so we can see who defaulted it. */
    GLboolean srgb_enabled = srgb_control_available() ? glIsEnabled(GL_FRAMEBUFFER_SRGB) : GL_FALSE;

    fprintf(stderr,
            "[diag] framebuffer frame=%lu pixels=%zu nonblack=%zu "
            "hash=%016" PRIx64 " gl_error=0x%x "
            "srgb=%s alpha_mean=%.4f min_dark=(R%u,G%u,B%u) p1=%u p5=%u "
            "hist[0..15]=%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n",
            frame, npixels, nonblack, hash, (unsigned int)error,
            (srgb_enabled ? "ON" : "off"),
            (double)alpha_sum / (double)npixels / 255.0,
            min_dark_r, min_dark_g, min_dark_b,
            p1, p5,
            hist[0], hist[1], hist[2], hist[3],
            hist[4], hist[5], hist[6], hist[7],
            hist[8], hist[9], hist[10], hist[11],
            hist[12], hist[13], hist[14], hist[15]);

    /* Optional PNG dump -- driven by NCZ_FRAME_DUMP. Format:
     *   NCZ_FRAME_DUMP=/abs/path  -- every captured frame written as
     *                                <frame>.png (8-digit, hex)
     * Useful for evidence validation against the existing pixel-diff
     * gate. The renderer's PNG encoder uses stb_image_write, which
     * is otherwise pulled in by the host's libpng path. */
    static int dump_checked;
    static int dump_enabled;
    static char dump_dir[512];
    if (!dump_checked) {
        const char *env = getenv("NCZ_FRAME_DUMP");
        if (env && *env) {
            dump_enabled = 1;
            snprintf(dump_dir, sizeof dump_dir, "%s", env);
            mkdir(dump_dir, 0755);
        }
        dump_checked = 1;
    }
    if (dump_enabled) {
        fprintf(stderr, "[dump] writing frame_%08lx to %s\n",
                (unsigned long)frame, dump_dir);
        /* Convert GL_RGBA (origin bottom-left) to a top-down RGBA layout. */
        unsigned char *flipped = malloc(npixels * 4);
        if (!flipped) {
            fprintf(stderr, "[dump] malloc for flipped RGBA failed\n");
            free(pixels);
            return;
        }
        for (size_t y = 0; y < (size_t)a->height; y++) {
            memcpy(flipped + y * (size_t)a->width * 4,
                   pixels + ((size_t)a->height - 1 - y) * (size_t)a->width * 4,
                   (size_t)a->width * 4);
        }
        char path[1024];
        snprintf(path, sizeof path, "%s/frame_%08lx.png",
                 dump_dir, (unsigned long)frame);
        /* Real PNG via libpng (dep_png). Previously we wrote raw RGBA
         * bytes with a .png suffix, which produced files nobody could
         * open. The PNG is now genuinely decodable. */
        if (ncz_write_png_rgba(path, flipped,
                               (unsigned int)a->width,
                               (unsigned int)a->height) == 0) {
            fprintf(stderr, "[dump] wrote %s (%zux%zu RGBA)\n",
                    path, (size_t)a->width, (size_t)a->height);
        } else {
            fprintf(stderr, "[dump] failed to write %s\n", path);
        }
        /* Also write the raw RGBA for downstream tools (pixel-diff etc.)
         * under a .rgba extension so callers don't confuse it with PNG. */
        char raw[1024];
        snprintf(raw, sizeof raw, "%s/frame_%08lx.rgba",
                 dump_dir, (unsigned long)frame);
        FILE *r = fopen(raw, "wb");
        if (r) { fwrite(flipped, 1, npixels * 4, r); fclose(r); }
        free(flipped);
    }
    free(pixels);
}

/* Frame 4 is a one-shot sample; periodic full-frame readbacks (a 70-110 ms
 * stall on Orion O6N) are opt-in via NCZ_DIAG_FRAMEBUFFER or NCZ_FRAME_DUMP, which
 * needs the frames. See ncz_diag_sample_frame() in ncz_gl.h. */
static int report_wanted(unsigned long idx) {
    static int dump = -1;
    static unsigned long every = 60;
    if (dump < 0) {
        const char *f = getenv("NCZ_FRAME_DUMP");
        dump = (f && *f && strcmp(f, "0") != 0) ? 1 : 0;
        /* NCZ_FRAME_DUMP_EVERY=N dumps every Nth frame instead of every
         * 60th, for time-lapse captures of long runs. */
        const char *e = dump ? getenv("NCZ_FRAME_DUMP_EVERY") : NULL;
        if (e && *e) {
            char *end = NULL;
            unsigned long n = strtoul(e, &end, 10);
            if (end && *end == 0 && n >= 1)
                every = n;
            else
                fprintf(stderr, "[dump] NCZ_FRAME_DUMP_EVERY=%s is not a positive integer; using 60\n", e);
        }
    }
    /* The first dump is at frame min(N, 60) or later: the opening frames are
     * not representative (shader warm-up, first readbacks). */
    if (dump && idx >= (every < 60 ? every : 60) && (idx % every) == 0)
        return 1;
    return ncz_diag_sample_frame(idx, 60);
}

static void draw_and_swap(struct app *a, unsigned long frame, unsigned long report_idx) {
    ncz_harness_attach_frame_size(a->width, a->height);
    hack->draw_cb(&a->mi);
    /* Sample the back buffer AFTER draw_cb wrote to it but BEFORE eglSwapBuffers
     * hands it to the compositor — that's the only moment we can read what
     * this frame actually rendered. We check `report_idx` (0-based frame
     * index, since _nframes was pre-incremented in the main loop) rather
     * than the 1-based `frame` so the boundary frame 4 + every 60th line up
     * with the validator's FIRST_FRAME=4 / SECOND_FRAME=60 grep.
     *
     * History: the original code had this check inside draw_and_swap with a
     * post-increment frame counter in the main loop, which meant frame 4
     * was never passed in — see the comment above the main loop for the
     * off-by-one history. */
    if (report_wanted(report_idx))
        report_framebuffer(a, report_idx);
    if (!eglSwapBuffers(a->egl_display, a->egl_surface)) {
        fprintf(stderr, "gles3_harness: eglSwapBuffers failed (0x%x)\n",
                (unsigned int)eglGetError());
        a->running = 0;
    }
}

/* ----------------------------------------------------------------------- */
/* EGL setup                                                               */
/* ----------------------------------------------------------------------- */

static void init_egl(struct app *a) {
    a->egl_display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR,
                                          a->display, NULL);
    if (a->egl_display == EGL_NO_DISPLAY) {
        a->egl_display = eglGetDisplay((EGLNativeDisplayType)a->display);
        if (a->egl_display == EGL_NO_DISPLAY) {
            fprintf(stderr, "gles3_harness: eglGetPlatformDisplay failed\n");
            ncz_harness_die(1);
        }
    }
    EGLint maj = 0, minn = 0;
    if (!eglInitialize(a->egl_display, &maj, &minn)) {
        fprintf(stderr, "gles3_harness: eglInitialize failed (0x%x)\n",
                (unsigned int)eglGetError());
        ncz_harness_die(1);
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "gles3_harness: eglBindAPI failed (0x%x)\n",
                (unsigned int)eglGetError());
        ncz_harness_die(1);
    }

    /* Probe whether EGL_KHR_gl_colorspace is exposed. The default
     * colorspace for an eglCreateWindowSurface call is up to the driver
     * when this extension is absent, which has historically been the
     * source of vendor-vs-vendor black-floor differences. Print the
     * extension presence to stderr so it's recorded in every run. */
    const char *ext_client = eglQueryString(a->egl_display, EGL_EXTENSIONS);
    if (!ext_client) ext_client = "";
    bool has_gl_colorspace =
        strstr(ext_client, "EGL_KHR_gl_colorspace") != NULL;
    g_gl_colorspace = has_gl_colorspace;
    fprintf(stderr,
            "[diag] colorspace_ext=EGL_KHR_gl_colorspace=%s "
            "requested=%s client_ext='%s'\n",
            has_gl_colorspace ? "yes" : "no",
            has_gl_colorspace ? "EGL_GL_COLORSPACE_LINEAR" : "driver-default",
            ext_client);

    a->egl_config = ncz_gles3_choose_config(a->egl_display);
    if (!a->egl_config) {
        fprintf(stderr, "gles3_harness: no GLES3 EGL configs\n");
        ncz_harness_die(1);
    }

    /* After choosing, READ BACK the config attributes the driver
     * actually granted. ncz_gles3_choose_config requests RGBA8+depth16
     * and, when EGL_KHR_gl_colorspace is exposed, an explicit LINEAR
     * (non-sRGB) EGL_GL_COLORSPACE. Linear is the contract our shaders
     * are written to: they emit display-referred sRGB values, so the
     * driver must not sRGB-encode them again. Some Mesa configs report
     * `colorspace=EGL_NONE` here even when the request is honoured, so
     * the authoritative signals are the `requested=EGL_GL_COLORSPACE_
     * LINEAR` startup line and `srgb=off` on each framebuffer report. */
    EGLint cfg_red=0, cfg_green=0, cfg_blue=0, cfg_alpha=0;
    EGLint cfg_depth=0, cfg_stencil=0, cfg_rtype=0;
    EGLint cfg_colorspace=EGL_NONE;  /* not always queryable on all drivers */
    EGLint cfg_buffer_type=EGL_NONE;
    eglGetConfigAttrib(a->egl_display, a->egl_config,
                       EGL_RED_SIZE,   &cfg_red);
    eglGetConfigAttrib(a->egl_display, a->egl_config,
                       EGL_GREEN_SIZE, &cfg_green);
    eglGetConfigAttrib(a->egl_display, a->egl_config,
                       EGL_BLUE_SIZE,  &cfg_blue);
    eglGetConfigAttrib(a->egl_display, a->egl_config,
                       EGL_ALPHA_SIZE, &cfg_alpha);
    eglGetConfigAttrib(a->egl_display, a->egl_config,
                       EGL_DEPTH_SIZE, &cfg_depth);
    eglGetConfigAttrib(a->egl_display, a->egl_config,
                       EGL_STENCIL_SIZE, &cfg_stencil);
    eglGetConfigAttrib(a->egl_display, a->egl_config,
                       EGL_RENDERABLE_TYPE, &cfg_rtype);
    /* EGL_GL_COLORSPACE is only valid when EGL_KHR_gl_colorspace is
     * exposed; on Mesa/AMD drivers it's usually supported but NVIDIA
     * historically only honors it on specific configs. Query with a
     * fallback so we don't crash. */
    if (has_gl_colorspace) {
        EGLint cs = EGL_NONE;
        if (eglGetConfigAttrib(a->egl_display, a->egl_config,
                               EGL_GL_COLORSPACE,
                               &cs) == EGL_TRUE) cfg_colorspace = cs;
    }
    eglGetConfigAttrib(a->egl_display, a->egl_config,
                       EGL_COLOR_BUFFER_TYPE, &cfg_buffer_type);
    fprintf(stderr,
            "[diag] eglconfig RGBA=%d/%d/%d/%d depth=%d stencil=%d "
            "rtype=0x%x colorspace=0x%x buf_type=0x%x\n",
            cfg_red, cfg_green, cfg_blue, cfg_alpha,
            cfg_depth, cfg_stencil,
            (unsigned int)cfg_rtype,
            (unsigned int)cfg_colorspace,
            (unsigned int)cfg_buffer_type);

    /* GLES 3.2 context — same client version as the runtime requires. */
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    a->egl_context = eglCreateContext(a->egl_display, a->egl_config,
                                      EGL_NO_CONTEXT, ctx_attr);
    if (a->egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "gles3_harness: eglCreateContext failed (0x%x)\n",
                (unsigned int)eglGetError());
        ncz_harness_die(1);
    }
    /* Probe the GL strings once the context is live (needs MakeCurrent,
     * which we do later when the surface arrives). For now log the
     * EGL version. */
    fprintf(stderr, "[diag] gles3_harness: EGL %d.%d, GLES3 context live\n",
            maj, minn);
}

/* ----------------------------------------------------------------------- */
/* Shutdown                                                                */
/* ----------------------------------------------------------------------- */

static void app_fini(struct app *a) {
    if (a->frame_cb) { wl_callback_destroy(a->frame_cb); a->frame_cb = NULL; }

    if (a->configured && hack->free_cb) {
        hack->free_cb(&a->mi);
    }

    if (a->egl_context != EGL_NO_CONTEXT) {
        eglMakeCurrent(a->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        eglDestroyContext(a->egl_display, a->egl_context);
        a->egl_context = EGL_NO_CONTEXT;
    }
    if (a->egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(a->egl_display, a->egl_surface);
        a->egl_surface = EGL_NO_SURFACE;
    }
    if (a->egl_window) {
        wl_egl_window_destroy(a->egl_window);
        a->egl_window = NULL;
    }
    if (a->egl_display != EGL_NO_DISPLAY) {
        eglTerminate(a->egl_display);
        a->egl_display = EGL_NO_DISPLAY;
    }
    if (a->keyboard)  { wl_keyboard_release(a->keyboard);  a->keyboard = NULL; }
    if (a->layer_surface) { zwlr_layer_surface_v1_destroy(a->layer_surface); a->layer_surface = NULL; }
    if (a->surface) { wl_surface_destroy(a->surface); a->surface = NULL; }
    if (a->xdg_toplevel) { xdg_toplevel_destroy(a->xdg_toplevel); a->xdg_toplevel = NULL; }
    if (a->xdg_surface)  { xdg_surface_destroy(a->xdg_surface);  a->xdg_surface  = NULL; }
    if (a->wm_base)      { xdg_wm_base_destroy(a->wm_base);      a->wm_base      = NULL; }
    if (a->layer_shell) { zwlr_layer_shell_v1_destroy(a->layer_shell); a->layer_shell = NULL; }
    if (a->seat) { wl_seat_release(a->seat); a->seat = NULL; }
    if (a->output) { wl_output_release(a->output); a->output = NULL; }
    if (a->compositor) { wl_compositor_destroy(a->compositor); a->compositor = NULL; }
    if (a->registry) { wl_registry_destroy(a->registry); a->registry = NULL; }
    if (a->display) { wl_display_disconnect(a->display); a->display = NULL; }
}

static void atexit_app_fini(void) {
    if (g_app) { app_fini(g_app); g_app = NULL; }
}

/* See the doc comment in ncz_gl.h. Safe to call at any point,
 * including before g_app is assigned (early setup failures). */
void ncz_harness_die(int code) {
    if (g_app) { app_fini(g_app); g_app = NULL; }
    exit(code);
}


/* Adaptive render scale: if the 95th percentile frame time stays above 40 ms
 * for 3 s, step the scale down 1 -> 0.75 -> 0.5, at most once per 3 s, never
 * back up within the run. Only in render-scale-mode=auto. */
static void adapt_render(struct app *a, unsigned long frame) {
    if (!ncz_cfg_scale_auto() || !a->viewporter || !a->configured) return;
    if (frame < 240 || (frame % 30) != 0) return;
    double t = mono_s();
    if (t - a->last_adapt_s < 3.0) return;
    double p95 = ncz_stats_p95_recent(3);
    if (p95 < 40.0) return;
    double ns = ncz_render_step_down(a->rscale);
    if (ns >= a->rscale - 1e-6) return;
    fprintf(stderr, "[diag] gles3_harness: adaptive render scale %.2f -> %.2f (p95 %.1f ms over 3 s)\n",
            a->rscale, ns, p95);
    a->rscale = ns;
    a->last_adapt_s = t;
    apply_render_size(a, a->nat_w, a->nat_h);
    commit_render_size(a);
}

static volatile sig_atomic_t g_stats_req;
static void on_usr1(int sig) { (void)sig; g_stats_req = 1; }

/* ----------------------------------------------------------------------- */
/* main                                                                    */
/* ----------------------------------------------------------------------- */

int main(int argc, char **argv) {
    /* Options, --help, --list-options, --version, --dump-schema: all handled
     * before any Wayland or EGL work. Unknown options are an error (exit 2). */
    /* Hack hook (weak): engines that randomize their own scene/preset re-roll
     * before the option set is resolved, so the rolled values flow through the
     * normal precedence chain. */
    extern void ncz_hack_preinit(int argc, char **argv) __attribute__((weak));
    if (ncz_hack_preinit) ncz_hack_preinit(argc, argv);
    int early = ncz_harness_cfg_init(argc, argv);
    if (early >= 0) return early;

    /* Off-screen frame-source mode (ncz-screensaver-feed): render this hack
     * into an FBO and stream frames over a Unix socket, with no compositor and
     * no Wayland connection. Selected per-process by NCZ_FEED_SOCKET, so a
     * normal run never enters it. See src/ncz_feed_render.c. */
    if (getenv("NCZ_FEED_SOCKET"))
        return ncz_feed_render_main(hack, argc, argv);

    ncz_stats_start();
    static struct app app;
    memset(&app, 0, sizeof app);
    app.egl_display = EGL_NO_DISPLAY;
    app.egl_surface = EGL_NO_SURFACE;
    app.egl_context = EGL_NO_CONTEXT;
    app.running = 1;

    g_app = &app;
    atexit(ncz_stats_print);
    if (atexit(atexit_app_fini) != 0) {
        fprintf(stderr, "gles3_harness: atexit failed\n");
        ncz_harness_die(1);
    }

    /* Signal handlers. */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGTERM);
    DIE(sigaction(SIGINT,  &sa, NULL), 0, "sigaction(SIGINT) failed");
    DIE(sigaction(SIGTERM, &sa, NULL), 0, "sigaction(SIGTERM) failed");
    struct sigaction ign;
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    DIE(sigaction(SIGPIPE, &ign, NULL), 0, "sigaction(SIGPIPE) failed");
    {
        struct sigaction u1;
        memset(&u1, 0, sizeof u1);
        u1.sa_handler = on_usr1;
        sigemptyset(&u1.sa_mask);
        sigaction(SIGUSR1, &u1, NULL);
    }

    /* Wayland connect + bind. */
    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "gles3_harness: wl_display_connect failed\n");
        ncz_harness_die(1);
    }
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &reg_listener, &app);
    if (wl_display_roundtrip(app.display) < 0) ncz_harness_die(1);
    if (!app.compositor || !app.seat || (!app.layer_shell && !app.wm_base)) {
        fprintf(stderr,
            "gles3_harness: missing Wayland globals "
            "(compositor=%d seat=%d layer_shell=%d xdg_wm_base=%d)\n",
            !!app.compositor, !!app.seat, !!app.layer_shell, !!app.wm_base);
        ncz_harness_die(1);
    }
    wl_seat_add_listener(app.seat, &seat_listener, &app);
    if (wl_display_roundtrip(app.display) < 0) ncz_harness_die(1);

    /* EGL. */
    init_egl(&app);

    /* The GLES3 runtime (shader + scratch VBO/VAO) needs a current
     * context. The hack's init_cb will set up state, but the runtime
     * itself must be live before any ncz_* helper runs. Bind a
     * placeholder current context — we don't have a surface yet, but
     * the EGL spec lets us bind the context with EGL_NO_SURFACE for
     * client-side compilation. */
    DIE(eglMakeCurrent(app.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       app.egl_context), EGL_TRUE,
        "eglMakeCurrent (no surface) failed");
    /* Never software render on a machine that has a GPU: fail before any shader work. */
    guard_renderer("startup");

    /* Explicitly disable GL_FRAMEBUFFER_SRGB. The GLES3.2 spec
     * default for this toggle is DISABLED — we're not opting into
     * sRGB writes — but Mesa and NVIDIA both report
     * `glIsEnabled(GL_FRAMEBUFFER_SRGB) = ON` here even though we
     * never called glEnable, because their tokens track the surface
     * tag rather than the glEnable state. Pinning this explicitly to
     * off removes one avenue for vendor drift and makes the harness
     * spec-conformant. After this call, report_framebuffer's
     * `srgb=ON|off` log will read `off` on every box. See
     * docs/audit/blackhole-black-floor-evidence/ROOTCAUSE.md §11. */
    if (srgb_control_available())
        glDisable(GL_FRAMEBUFFER_SRGB);

    /* Log GL strings now. */
    fprintf(stderr, "[diag] GL_VERSION=%s\nRENDERER=%s\nVENDOR=%s\nGLSL=%s\n",
            (const char*)glGetString(GL_VERSION),
            (const char*)glGetString(GL_RENDERER),
            (const char*)glGetString(GL_VENDOR),
            (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION));
    /* Headless run (NCZ_HARNESS_HEADLESS=WxH, or 1 for 256x144): never create a
     * Wayland surface, so nothing can appear on screen or take keyboard focus
     * (an overlay layer surface with exclusive keyboard interactivity closes
     * the Singularity settings sidebar even when no buffer is ever attached).
     * The context stays surfaceless: the default framebuffer is unusable, which
     * is exactly right for hacks whose capture mode renders into its own
     * framebuffer and exits by itself (Black Hole --capture, used by
     * `ncz-screensaver still` for the settings preview). The hack is driven by
     * repeated draw_cb calls with no swap; a hack that never exits is stopped
     * after a bounded number of draws so a misuse fails instead of spinning. */
    const char *headless = getenv("NCZ_HARNESS_HEADLESS");
    if (headless && *headless && strcmp(headless, "0") != 0) {
        int hw = 0, hh = 0;
        if (sscanf(headless, "%dx%d", &hw, &hh) != 2 || hw < 16 || hh < 16 ||
            hw > 16384 || hh > 16384) {
            hw = 256;
            hh = 144;
        }
        app.width = hw;
        app.height = hh;
        app.nat_w = hw;
        app.nat_h = hh;
        fprintf(stderr, "[diag] gles3_harness: headless %dx%d (no surface)\n", hw, hh);
        hack_init_once(&app);
        for (unsigned long i = 0; i < 100000UL && app.running; i++)
            hack->draw_cb(&app.mi);
        fprintf(stderr, "gles3_harness: headless run ended without the hack exiting\n");
        app_fini(&app);
        g_app = NULL;
        return 1;
    }

    /* Detach for the surface-config phase to rebind to the layer
     * surface. */
    eglMakeCurrent(app.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);

    /* Layer surface. */
    app.surface = wl_compositor_create_surface(app.compositor);
    if (!app.surface) {
        fprintf(stderr, "gles3_harness: create_surface failed\n");
        ncz_harness_die(1);
    }
    if (!app.layer_shell) {
        fprintf(stderr, "[diag] no zwlr_layer_shell_v1; "
                        "falling back to a fullscreen xdg_toplevel\n");
        app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
        if (!app.xdg_surface) {
            fprintf(stderr, "gles3_harness: get_xdg_surface failed\n");
            ncz_harness_die(1);
        }
        xdg_surface_add_listener(app.xdg_surface, &xdg_surf_listener, &app);
        app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
        if (!app.xdg_toplevel) {
            fprintf(stderr, "gles3_harness: get_toplevel failed\n");
            ncz_harness_die(1);
        }
        xdg_toplevel_add_listener(app.xdg_toplevel, &xdg_top_listener, &app);
        xdg_toplevel_set_title(app.xdg_toplevel, "ncz-gles3");
        xdg_toplevel_set_app_id(app.xdg_toplevel, "org.nclawzero.screensaver");
        /* NULL, not app.output: reg_global binds whichever wl_output the
         * registry happens to advertise first, which on a multi-connector
         * board (Orion O6N: DP-1/DP-2/DP-3 + 6 writeback pseudo-outputs) is not
         * necessarily the one actually connected to a monitor. Passing NULL
         * tells the compositor to pick a real output itself, per the
         * xdg_toplevel.set_fullscreen protocol spec. Fixed 2026-09-22 after
         * a live Orion O6N test rendered 4500+ correct frames with zero errors
         * while showing nothing on the actual (DP-2) display -- it was
         * fullscreening onto a disconnected output the whole time. */
        xdg_toplevel_set_fullscreen(app.xdg_toplevel, NULL);
        wl_surface_commit(app.surface);
        for (int i = 0; i < 50 && !app.configured; i++) {
            if (wl_display_roundtrip(app.display) < 0) ncz_harness_die(1);
        }
        goto shell_ready;
    }

    /* NULL output -- see the comment on the xdg_toplevel_set_fullscreen
     * fallback path above; same reasoning applies to the layer-shell path. */
    app.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        app.layer_shell, app.surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "gles3-hack");
    if (!app.layer_surface) {
        fprintf(stderr, "gles3_harness: get_layer_surface failed\n");
        ncz_harness_die(1);
    }
    zwlr_layer_surface_v1_set_anchor(app.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(app.layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        app.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    zwlr_layer_surface_v1_add_listener(app.layer_surface, &ls_listener, &app);
    wl_surface_commit(app.surface);
    /* A single roundtrip is not always enough: some compositor/renderer
     * combinations (observed on nvidia's proprietary EGL path) take more
     * than one dispatch cycle to emit zwlr_layer_surface_v1.configure.
     * Keep round-tripping, bounded, rather than assuming one is enough. */
    for (int i = 0; i < 50 && !app.configured; i++) {
        if (wl_display_roundtrip(app.display) < 0) ncz_harness_die(1);
    }

shell_ready:
    if (!app.configured) {
        fprintf(stderr, "gles3_harness: never configured\n");
        ncz_harness_die(1);
    }

    /* Draw + swap once BEFORE asking for a frame callback. See the
     * long comment in glmatrix_harness.c — the same applies here. */
    fprintf(stderr, "[diag] initial draw: %dx%d configured=%d\n",
            app.width, app.height, (int)app.configured);
    hack->draw_cb(&app.mi);
    fprintf(stderr, "[diag] initial draw_cb returned; swapping\n");
    if (!eglSwapBuffers(app.egl_display, app.egl_surface)) {
        fprintf(stderr, "gles3_harness: initial eglSwapBuffers failed (0x%x)\n",
                (unsigned int)eglGetError());
        ncz_harness_die(1);
        return 1;
    }
    {
        static unsigned long _nframes = 0;
        while (app.running) {
            if (wl_display_dispatch_pending(app.display) < 0) {
                app.running = 0;
                break;
            }
            if (wl_display_flush(app.display) < 0 && errno != EAGAIN) {
                app.running = 0;
                break;
            }
            _nframes++;
            /* Print "frame #N" for the first 5 frames and every 60th.
             * The "N" printed is the call number of the upcoming draw_and_swap
             * (0-based), matching the convention the validator expects in its
             * [diag] framebuffer frame=N grep. Also call report_framebuffer
             * at frame 4 and every 60th frame so the validator sees the
             * nonblack-pixel sample it needs to distinguish PASS from FAIL.
             *
             * Previously this used a post-increment pattern
             * (`fprintf frame #N; _nframes++; draw_and_swap(N+1)`) which
             * skipped frame 4 entirely — the validator's FIRST_FRAME=4
             * grep never matched and every rss-sdl2 hack got labeled
             * FAIL black despite rendering correctly. See
             * docs/superpowers/rounds/2026-09-26-rss-sdl2-rootcause.md. */
            unsigned long _report_idx = _nframes - 1;
            if (_report_idx < 5 || (_report_idx > 0 && (_report_idx % 60) == 0))
                fprintf(stderr, "[diag] frame #%lu\n", _report_idx);
            draw_and_swap(&app, _nframes, _report_idx);
            ncz_stats_frame();
            if (g_stats_req) { g_stats_req = 0; ncz_stats_print(); }
            adapt_render(&app, _nframes);
        }
    }
    if (app.display) wl_display_roundtrip(app.display);

    /* Run cleanup explicitly here, synchronously, before main() returns
     * and exit() atexit chain runs. Previously this relied solely on
     * atexit(atexit_app_fini) -- but atexit handlers run LIFO, and the
     * NVIDIA GLES/EGL driver registers its own atexit/destructor
     * teardown during context creation, which lands AFTER ours in
     * registration order and therefore runs BEFORE ours at exit time.
     * That tore down GL dispatch trampolines out from under our
     * glDelete* calls -- confirmed via gdb: SIGSEGV jumping to an
     * unmapped address (Cannot access memory) inside
     * ncz_gles3_runtime_fini(), reproduced identically on NVIDIA RTX
     * 2060 and RTX 4500 Ada, never on Mesa/Mali. Calling app_fini()
     * here runs our GL teardown before any other library atexit
     * chain begins; atexit_app_fini g_app-NULL guard makes the
     * still-registered atexit call a safe no-op afterward. */
    app_fini(&app);
    g_app = NULL;
    return 0;
}
