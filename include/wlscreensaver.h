/*
 * wlscreensaver — a Wayland + GL screensaver engine, as an embeddable library.
 *
 * PURPOSE OF THIS HEADER
 * ----------------------
 * This is the integration surface. A host program — a lock screen, a
 * screensaver daemon, a settings preview widget — owns its own Wayland
 * connection and surface, and hands them to the engine. The engine owns EGL,
 * the GL context and the visual effect, and nothing else.
 *
 * It is deliberately a LIBRARY and not a separate process. ext-session-lock-v1
 * permits exactly one lock client per session, and Wayland subsurfaces must
 * come from the same wl_client, so a second process cannot attach a surface
 * beneath the host's prompt. The renderer therefore has to live inside the
 * host's client. That is a constraint of the protocol, not a preference.
 *
 * WHAT THE ENGINE DOES NOT DO
 * ---------------------------
 * It does not connect to Wayland, bind globals, create surfaces, choose a
 * shell (layer-shell, xdg-shell, session-lock), handle input, or authenticate
 * anyone. Those belong to the host, which knows what kind of window it is. The
 * engine is handed a surface and a size and draws into it.
 *
 * This split is what keeps the engine environment-agnostic: the common
 * denominator is Wayland and GL, so any compositor and any desktop can host it.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef WLSCREENSAVER_H
#define WLSCREENSAVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wl_display;
struct wl_surface;

typedef struct wlss_engine wlss_engine;

/* ------------------------------------------------------------------------ */
/* Effects                                                                  */
/* ------------------------------------------------------------------------ */

/**
 * Names of the compiled-in effects, NULL-terminated. Stable strings owned by
 * the library; do not free.
 *
 * Effects are ported xscreensaver hacks. They are third-party C running legacy
 * fixed-function GL through a translation layer, which is exactly why the
 * watchdog below exists.
 */
const char *const *wlss_effect_names(void);

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                */
/* ------------------------------------------------------------------------ */

typedef struct {
    /** Wayland display the host is already connected to. Required. */
    struct wl_display *display;

    /** Surface to render into. The host owns it and its shell role. Required. */
    struct wl_surface *surface;

    /**
     * Surface size in LOGICAL coordinates, as the shell reported it, and the
     * output scale.
     *
     * THESE ARE NOT THE SAME NUMBER AND CONFLATING THEM IS THE MOST COMMON
     * WAY TO GET THIS WRONG. Measured on a Radxa O6N: the greeter, running
     * before the session's scale applies, is configured at 3840x2160; a
     * surface inside the same session on the same panel is configured at
     * 2194x1234, because the output carries a 1.75 fractional scale. A host
     * that passes logical size and no scale renders at 57% of the panel's
     * resolution and the compositor upscales the result, which looks exactly
     * like "the lock screen came up at the wrong resolution".
     *
     * Pass the logical size the shell gave you and the scale of the output the
     * surface is on. The engine sizes its EGL window in real pixels and sets
     * the buffer scale.
     *
     * scale is in 120ths (wp_fractional_scale_v1 units): 120 = 1.0, 210 = 1.75.
     * Pass 120 if the compositor offers no fractional scale.
     */
    uint32_t logical_width;
    uint32_t logical_height;
    uint32_t scale_120;

    /** Effect name from wlss_effect_names(). NULL selects the default. */
    const char *effect;

    /**
     * Optional effect settings, as "key=value" strings, NULL-terminated.
     * Unknown keys are ignored. e.g. { "density=60", "fog=false", NULL }
     */
    const char *const *options;
} wlss_config;

/**
 * Create an engine and bring up EGL against the host's surface.
 *
 * Returns NULL on failure; *err_out (if non-NULL) receives a static
 * human-readable reason. Does NOT draw anything and does NOT commit the
 * surface — the host controls when the first frame appears.
 */
wlss_engine *wlss_create(const wlss_config *cfg, const char **err_out);

/** Destroy the engine and release its GL and EGL resources. Safe on NULL. */
void wlss_destroy(wlss_engine *e);

/**
 * Tell the engine the surface geometry changed.
 *
 * Call on every shell configure, including scale changes. Cheap and idempotent
 * when nothing actually changed.
 */
void wlss_resize(wlss_engine *e, uint32_t logical_width,
                 uint32_t logical_height, uint32_t scale_120);

/**
 * Render one frame and swap.
 *
 * The host drives this from its own frame callback, so the host keeps control
 * of pacing and can stop drawing when the surface is hidden. Returns false if
 * the frame could not be drawn, in which case the host should treat the engine
 * as failed and fall back (see the watchdog note below).
 */
bool wlss_frame(wlss_engine *e, uint64_t time_ms);

/* ------------------------------------------------------------------------ */
/* Failure containment                                                      */
/* ------------------------------------------------------------------------ */

/**
 * WHY A HOST MUST PLAN FOR THE ENGINE DYING.
 *
 * On a lock surface this is a security property, not a quality one. The effect
 * is third-party C; if it crashes or wedges inside the process that owns the
 * lock surface, the whole locker goes with it. Depending on the compositor
 * that either exposes the desktop or — measured on labwc — leaves the session
 * locked with NO client drawing anything, which only a compositor restart
 * clears.
 *
 * A host that owns a lock surface must therefore:
 *
 *   - keep the authentication path in code the engine cannot reach,
 *   - treat a false return from wlss_frame() as fatal to the ENGINE only,
 *     dropping to a static background while the prompt stays live,
 *   - bound the time a frame may take, and abandon the engine if exceeded.
 *
 * A dead renderer must never leave the session unlocked, and must never leave
 * a blank screen with no way to authenticate.
 *
 * wlss_watchdog_ms() reports the engine's own advisory budget for a single
 * frame; hosts are free to impose a stricter one.
 */
uint32_t wlss_watchdog_ms(const wlss_engine *e);

/** Last error, or NULL. Static string; valid until the next engine call. */
const char *wlss_last_error(const wlss_engine *e);

#ifdef __cplusplus
}
#endif
#endif /* WLSCREENSAVER_H */
