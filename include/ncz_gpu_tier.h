/* ncz_gpu_tier.h — shared GPU capability detection + adaptive quality tier.
 *
 * Per the catalogue doctrine
 * (docs/superpowers/specs/2026-09-25-screensaver-family-doctrine.md),
 * every engine exposes a quality ladder that ADDS on strong hardware, not
 * only subtracts. Today the ladders are aspirational — pieces declare
 * tiers in documentation but render the same thing regardless. This
 * module is the runtime that backs those tiers.
 *
 * Two stages:
 *   1. Static prior from GL_RENDERER/VENDOR/VERSION + a couple of cheap
 *      GL limits. This is a STARTING tier — never the answer. "Intel
 *      UHD Graphics" spans many generations with very different
 *      throughput, and the same GPU performs differently at 1080p and
 *      4K. A renderer string is a hint, not a measurement.
 *   2. Runtime measurement via rolling-median frame time. The engine
 *      calls ncz_gpu_tier_record_frame_ms() each draw; the module
 *      compares against the target budget (~16.7ms / 60fps) and adapts
 *      tier with hysteresis.
 *
 * Hysteresis:
 *   - Warm up: ignore the first ~30 frames (shader compile, cache
 *     populate, first-draw costs). A cold frame is not representative.
 *   - Track a rolling median, not a mean — a single hitch should not
 *     move the tier.
 *   - Asymmetric thresholds: downgrade readily when over budget,
 *     upgrade only when comfortably under it.
 *   - Rate-limit changes; never oscillate.
 *   - Settle: once we have been running for a minute, never upgrade.
 *
 * Override:
 *   NCZ_GPU_TIER=low|medium|high|ultra|auto (default auto).
 *   Used for reproducible captures and for tier-comparison evidence.
 *
 * Usage:
 *   in init_cb, once GL is current:
 *     ncz_gpu_tier_init();                      reads override + queries GL hints
 *   each frame in draw_cb:
 *     double dt_ms = ncz_now() - prev;
 *     ncz_gpu_tier_record_frame_ms(dt_ms);
 *     const ncz_gpu_tier_t *t = ncz_gpu_tier_current();
 *     read t->tier, t->scalar, t->name, t->reason for knobs
 *   once on shutdown (optional):
 *     ncz_gpu_tier_shutdown();
 */

#ifndef NCZ_GPU_TIER_H
#define NCZ_GPU_TIER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tiers are ordered low -> ultra. The integer is the order; the scalar
 * is a normalised quality level that each engine maps to its own knobs
 * (the engine is the right place to know what 0.33 means for its budget). */
typedef enum {
    NCZ_TIER_LOW    = 0,
    NCZ_TIER_MEDIUM = 1,
    NCZ_TIER_HIGH   = 2,
    NCZ_TIER_ULTRA  = 3,
} ncz_tier_e;

#define NCZ_TIER_COUNT 4

typedef struct {
    ncz_tier_e tier;          /* current tier */
    float      scalar;        /* normalised: 0.0 low, 0.33 medium, 0.66 high, 1.0 ultra */
    /* tier_name is one of the static strings below; do not free. */
    const char *name;         /* "low" | "medium" | "high" | "ultra" */
    int        forced;        /* 1 if user override (NCZ_GPU_TIER) is active */
    int        downgraded;    /* 1 if a runtime measurement dropped the tier below the static prior */
    char       reason[160];   /* one-line description of why we are at this tier (buffer is owned) */
    /* For evidence / [diag] log lines: */
    double     median_ms;     /* rolling median frame time in ms (0 if not yet sampled) */
    int        sample_count;  /* frames observed since init (post-warmup) */
} ncz_gpu_tier_t;

/* Initialise the tier module. Safe to call multiple times (idempotent);
 * a subsequent call re-reads NCZ_GPU_TIER and re-queries the GL prior
 * — useful for engines that want to re-evaluate at init time. */
void ncz_gpu_tier_init(void);

/* Record one frame's duration in milliseconds. Should be called once
 * per draw_cb. The module maintains a rolling window and emits tier
 * changes after the appropriate dwell time. */
void ncz_gpu_tier_record_frame_ms(double dt_ms);

/* Snapshot of the current tier. Pointer is owned by the module; do
 * not free. Safe to call before any frame is recorded — you will get
 * the static-prior tier with median_ms == 0 and reason mentioning the
 * prior. */
const ncz_gpu_tier_t *ncz_gpu_tier_current(void);

/* Free internal state. Optional — engines don't have to call it. */
void ncz_gpu_tier_shutdown(void);

/* Helpers used by individual engines to map the scalar to their own
 * integer counts. Linear interpolation between two endpoints so the
 * transition between tiers is continuous at the engine level. */
int ncz_gpu_tier_count(int low_value, int high_value);
float ncz_gpu_tier_float(float low_value, float high_value);

/* Internal: rung-table scalars for the four named tiers. Exposed so
 * tests can assert that the ladder rises monotonically. */
float ncz_gpu_tier_scalar_for(ncz_tier_e t);

#ifdef __cplusplus
}
#endif

#endif /* NCZ_GPU_TIER_H */
