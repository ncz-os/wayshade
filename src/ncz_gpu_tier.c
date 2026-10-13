/* SPDX-License-Identifier: Apache-2.0 */
/* ncz_gpu_tier.c — implementation of the adaptive quality tier module.
 *
 * See include/ncz_gpu_tier.h for the contract.
 *
 * Design notes (kept here so future maintainers don't have to re-derive):
 *
 * The rolling window is small (60 frames ~= 1s at 60fps, ~2s at 30fps)
 * so the median is responsive enough to catch a sustained condition
 * within the dwell time without being fooled by single hitches. The
 * dwell time is the gate: 1.5s of sustained over-budget for downgrade,
 * 4.0s sustained under 0.65x budget for upgrade. Asymmetric because
 * oscillation is worse than a wrong tier.
 *
 * The "settle" rule (never upgrade after 60s of wallclock) is the
 * hardest part of the design. Without it, a piece that hit a warm
 * cache mid-session would jump tiers and the user would notice the
 * visual change. With it, the tier chosen in the first minute is the
 * tier for the session. This matches the operator's intent: the
 * ladder is for the showpiece moment, not a knob the user sees move.
 *
 * Override handling: forced tiers skip measurement entirely and the
 * recorded frames only update the median/sample_count for evidence.
 * That way [diag] lines still report timing even when the user is
 * forcing a tier.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <GLES3/gl32.h>

#include "ncz_gpu_tier.h"
#include "ncz_platform.h"

/* ---- Configuration constants (tunable, but tested as-is) ---- */

#define WARMUP_FRAMES         30     /* ignore the first N frames */
#define WINDOW_FRAMES         60     /* rolling window size */
#define TARGET_FRAME_MS_60    16.7   /* 60fps target */
#define TARGET_FRAME_MS_30    33.0   /* 30fps fallback target */
#define DOWNGRADE_OVER_MS     19.0   /* >19ms for >1.5s -> downgrade */
#define UPGRADE_UNDER_MS      10.5   /* <10.5ms for >4.0s -> upgrade */
#define DOWNGRADE_DWELL_S     1.5    /* seconds of sustained over-budget */
#define UPGRADE_DWELL_S       4.0    /* seconds of sustained under-budget */
#define SETTLE_S              60.0   /* never upgrade after this many seconds */
#define RATE_LIMIT_S          5.0    /* minimum seconds between tier changes */

/* ---- Internal state ---- */

typedef struct {
    ncz_gpu_tier_t current;
    int initialised;

    /* Override */
    int forced;              /* 1 if NCZ_GPU_TIER forced a tier */
    ncz_tier_e forced_tier;

    /* Static prior (the GL hint) */
    ncz_tier_e prior;
    char       prior_reason[128];

    /* Rolling window of recent frame times (in ms) */
    double window[WINDOW_FRAMES];
    int    window_count;     /* how many slots filled (up to WINDOW_FRAMES) */
    int    window_index;     /* next slot to write */
    int    warmup_remaining; /* frames to ignore at start */

    /* Timing */
    double session_started;  /* monotonic seconds, when init was called */
    double last_change_s;     /* monotonic seconds of last tier change */

    /* Dwell counters */
    int    over_budget_streak;   /* consecutive frames over DOWNGRADE_OVER_MS */
    int    under_budget_streak;  /* consecutive frames under UPGRADE_UNDER_MS */
    double over_since_s;         /* monotonic seconds when over-budget streak began */
    double under_since_s;        /* monotonic seconds when under-budget streak began */
} tier_state_t;

static tier_state_t g_state;

/* ---- Helpers ---- */

static double mono_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/* Sort a copy of the window and return the median. The window is at
 * most 60 doubles so insertion sort is fine and avoids qsort overhead
 * per frame. Returns 0 if window is empty. */
static double window_median(const tier_state_t *st) {
    if (st->window_count <= 0) return 0.0;
    double tmp[WINDOW_FRAMES];
    memcpy(tmp, st->window, (size_t)st->window_count * sizeof(double));
    /* Insertion sort. */
    for (int i = 1; i < st->window_count; i++) {
        double k = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j] > k) { tmp[j + 1] = tmp[j]; j--; }
        tmp[j + 1] = k;
    }
    int n = st->window_count;
    if (n % 2) return tmp[n / 2];
    return 0.5 * (tmp[n / 2 - 1] + tmp[n / 2]);
}

static void set_scalar_and_name(ncz_gpu_tier_t *t) {
    switch (t->tier) {
        case NCZ_TIER_LOW:    t->scalar = 0.0f;   t->name = "low";    break;
        case NCZ_TIER_MEDIUM: t->scalar = 0.33f;  t->name = "medium"; break;
        case NCZ_TIER_HIGH:   t->scalar = 0.66f;  t->name = "high";   break;
        case NCZ_TIER_ULTRA:  t->scalar = 1.0f;   t->name = "ultra";  break;
        default:              t->scalar = 0.0f;   t->name = "low";    t->tier = NCZ_TIER_LOW; break;
    }
}

/* Render a one-line description into current.reason. We keep it short
 * — the engine appends this to its own [diag] line. */
static void refresh_reason_with_prev(ncz_tier_e prev) {
    const char *base = g_state.prior_reason;
    if (g_state.forced) {
        snprintf(g_state.current.reason, sizeof(g_state.current.reason),
                 "forced via NCZ_GPU_TIER=%s", g_state.current.name);
        return;
    }
    if (g_state.current.downgraded) {
        const char *prev_name =
            (prev == NCZ_TIER_LOW)    ? "low"    :
            (prev == NCZ_TIER_MEDIUM) ? "medium" :
            (prev == NCZ_TIER_HIGH)   ? "high"   : "ultra";
        snprintf(g_state.current.reason, sizeof(g_state.current.reason),
                 "downgraded from %s (%.1fms median)", prev_name, g_state.current.median_ms);
        return;
    }
    snprintf(g_state.current.reason, sizeof(g_state.current.reason),
             "static prior (%s)", base ? base : "default");
}

/* ---- Static prior from GL strings ----
 *
 * The rules here are deliberately conservative — they only push the
 * tier DOWN from medium. If the renderer string is unrecognised we
 * start at medium and let runtime measurement move us either way.
 */
static void query_static_prior(void) {
    const char *vendor   = (const char *)glGetString(GL_VENDOR);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *version  = (const char *)glGetString(GL_VERSION);

    /* Cheap GL limits — push down if they are small. */
    GLint max_tex = 0;
    GLint max_uni = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &max_uni);

    /* Default: medium. */
    g_state.prior = NCZ_TIER_MEDIUM;
    snprintf(g_state.prior_reason, sizeof(g_state.prior_reason),
             "default=medium (vendor=%s renderer=%s version=%s tex=%d uni=%d)",
             vendor   ? vendor   : "?",
             renderer ? renderer : "?",
             version  ? version  : "?",
             (int)max_tex, (int)max_uni);

    if (!renderer) renderer = "";
    if (!vendor)   vendor   = "";

    /* Lower-end Intel iGPUs: UHD 600/620/630/770, Iris, etc. */
    if (strstr(renderer, "Intel") || strstr(vendor, "Intel")) {
        /* Intel(R) UHD Graphics (XXX) — anything in the 600/700 range. */
        if (strstr(renderer, "UHD") || strstr(renderer, "Iris") ||
            strstr(renderer, "HD Graphics")) {
            g_state.prior = NCZ_TIER_LOW;
            snprintf(g_state.prior_reason, sizeof(g_state.prior_reason),
                     "Intel iGPU detected: %s (tex=%d uni=%d) -> low",
                     renderer, (int)max_tex, (int)max_uni);
        }
    }

    /* Older / weaker mobile chips: Mali-T, Adreno 3xx-5xx era. */
    if (strstr(renderer, "Mali-T") || strstr(renderer, "Adreno 3") ||
        strstr(renderer, "Adreno 4") || strstr(renderer, "Adreno 5")) {
        g_state.prior = NCZ_TIER_LOW;
        snprintf(g_state.prior_reason, sizeof(g_state.prior_reason),
                 "weak mobile GPU: %s -> low", renderer);
    }

    /* Discrete NVIDIA / AMD on a desktop usually has headroom. The
     * measure stage will catch the slow case; here we just don't
     * artificially cap. */
    if (strstr(renderer, "GeForce") || strstr(renderer, "Radeon") ||
        strstr(renderer, "Quadro")) {
        g_state.prior = NCZ_TIER_HIGH;
        snprintf(g_state.prior_reason, sizeof(g_state.prior_reason),
                 "discrete GPU detected: %s -> high", renderer);
    }

    /* Very small GL_MAX_TEXTURE_SIZE is a hard floor: anything below
     * 2048 is in trouble at 1080p+. Override even a "high" prior. */
    if (max_tex > 0 && max_tex < 2048) {
        g_state.prior = NCZ_TIER_LOW;
        snprintf(g_state.prior_reason, sizeof(g_state.prior_reason),
                 "GL_MAX_TEXTURE_SIZE=%d is small -> low", (int)max_tex);
    }
}

/* ---- Tier change logic ---- */

static void apply_tier(ncz_tier_e t, int downgraded_flag) {
    /* Track the prior so refresh_reason can describe the change. */
    ncz_tier_e prev = g_state.current.tier;
    g_state.current.tier       = t;
    g_state.current.downgraded = downgraded_flag;
    /* Stash the prev so refresh_reason can reference it; refreshed after
     * set_scalar_and_name mutates name. */
    g_state.current.reason[0] = 0;
    set_scalar_and_name(&g_state.current);
    refresh_reason_with_prev(prev);
    g_state.last_change_s = mono_now();
}

static void consider_tier_change(void) {
    /* Forced or settled: do nothing. */
    if (g_state.forced) return;

    /* Not enough samples? Wait. */
    if (g_state.window_count < 10) return;

    double now = mono_now();
    double elapsed_since_start = now - g_state.session_started;
    if (elapsed_since_start < 0) elapsed_since_start = 0;

    double median = window_median(&g_state);
    g_state.current.median_ms = median;
    g_state.current.sample_count += 1;

    /* Rate limit: no change within RATE_LIMIT_S of the last. */
    if (now - g_state.last_change_s < RATE_LIMIT_S) return;

    /* OVER BUDGET -> track streak. */
    int over = (median > DOWNGRADE_OVER_MS) ? 1 : 0;
    if (over) {
        if (!g_state.over_budget_streak) g_state.over_since_s = now;
        g_state.over_budget_streak += 1;
        g_state.under_budget_streak  = 0;
    } else {
        g_state.over_budget_streak = 0;
    }

    /* UNDER BUDGET -> track streak. */
    int under = (median < UPGRADE_UNDER_MS) ? 1 : 0;
    if (under) {
        if (!g_state.under_budget_streak) g_state.under_since_s = now;
        g_state.under_budget_streak += 1;
    } else {
        g_state.under_budget_streak = 0;
    }

    /* Downgrade check: sustained over-budget for DOWNGRADE_DWELL_S. */
    if (g_state.over_budget_streak > 0 && g_state.current.tier > NCZ_TIER_LOW) {
        double dwell = now - g_state.over_since_s;
        if (dwell >= DOWNGRADE_DWELL_S) {
            ncz_tier_e next = (ncz_tier_e)(g_state.current.tier - 1);
            apply_tier(next, /*downgraded=*/1);
            g_state.over_budget_streak = 0;
            g_state.under_budget_streak = 0;
            return;
        }
    }

    /* Upgrade check: settled? Then NEVER upgrade. */
    if (elapsed_since_start >= SETTLE_S) {
        g_state.under_budget_streak = 0;
        return;
    }
    if (g_state.under_budget_streak > 0 && g_state.current.tier < NCZ_TIER_ULTRA) {
        double dwell = now - g_state.under_since_s;
        if (dwell >= UPGRADE_DWELL_S) {
            ncz_tier_e next = (ncz_tier_e)(g_state.current.tier + 1);
            apply_tier(next, /*downgraded=*/0);
            g_state.over_budget_streak = 0;
            g_state.under_budget_streak = 0;
            return;
        }
    }
}

/* ---- Public API ---- */

void ncz_gpu_tier_init(void) {
    /* Reset all state. Safe to call repeatedly — useful for tests. */
    memset(&g_state, 0, sizeof(g_state));
    g_state.session_started = mono_now();
    g_state.last_change_s   = mono_now() - RATE_LIMIT_S;  /* first change is not rate-limited */
    g_state.warmup_remaining = WARMUP_FRAMES;

    /* Default current tier is medium; the override or prior will set it. */
    g_state.current.tier = NCZ_TIER_MEDIUM;
    set_scalar_and_name(&g_state.current);

    /* Read the override. */
    const char *env = getenv("NCZ_GPU_TIER");
    if (env && *env) {
        if      (!strcasecmp(env, "low"))    { g_state.forced = 1; g_state.forced_tier = NCZ_TIER_LOW;    }
        else if (!strcasecmp(env, "medium")) { g_state.forced = 1; g_state.forced_tier = NCZ_TIER_MEDIUM; }
        else if (!strcasecmp(env, "high"))   { g_state.forced = 1; g_state.forced_tier = NCZ_TIER_HIGH;   }
        else if (!strcasecmp(env, "ultra"))  { g_state.forced = 1; g_state.forced_tier = NCZ_TIER_ULTRA;  }
        /* "auto" or anything unrecognised -> forced = 0 */
    }

    /* Query the GL prior — needs a current GL context. */
    query_static_prior();

    /* Apply either the forced tier or the prior as the starting point. */
    if (g_state.forced) {
        apply_tier(g_state.forced_tier, /*downgraded=*/0);
    } else {
        apply_tier(g_state.prior, /*downgraded=*/0);
    }

    g_state.current.median_ms   = 0.0;
    g_state.current.sample_count = 0;
    g_state.current.forced      = g_state.forced;
    g_state.initialised = 1;

    /* One-shot [diag] log so the operator can attribute captures. */
    ncz_log_diag("gpu_tier init override=%s tier=%s scalar=%.2f reason=%s",
                 env && *env ? env : "auto",
                 g_state.current.name,
                 g_state.current.scalar,
                 g_state.current.reason);
}

void ncz_gpu_tier_record_frame_ms(double dt_ms) {
    if (!g_state.initialised) ncz_gpu_tier_init();
    if (dt_ms <= 0.0) return;  /* ignore junk */
    if (dt_ms > 1000.0) dt_ms = 1000.0;  /* clamp first-frame outliers */

    /* Warm up: ignore the first N frames. */
    if (g_state.warmup_remaining > 0) {
        g_state.warmup_remaining -= 1;
        return;
    }

    /* Record into the rolling window. */
    g_state.window[g_state.window_index] = dt_ms;
    g_state.window_index = (g_state.window_index + 1) % WINDOW_FRAMES;
    if (g_state.window_count < WINDOW_FRAMES) g_state.window_count += 1;

    /* Decide whether to change tier. */
    consider_tier_change();

    /* Update the snapshot fields so current() callers see fresh values. */
    g_state.current.median_ms    = window_median(&g_state);
    g_state.current.sample_count += 1;
}

const ncz_gpu_tier_t *ncz_gpu_tier_current(void) {
    if (!g_state.initialised) ncz_gpu_tier_init();
    return &g_state.current;
}

void ncz_gpu_tier_shutdown(void) {
    memset(&g_state, 0, sizeof(g_state));
}

int ncz_gpu_tier_count(int low_value, int high_value) {
    const ncz_gpu_tier_t *t = ncz_gpu_tier_current();
    float k = t->scalar;   /* 0.0 .. 1.0 */
    float v = (float)low_value + k * ((float)high_value - (float)low_value);
    if (v < (float)low_value)  v = (float)low_value;
    if (v > (float)high_value) v = (float)high_value;
    return (int)(v + 0.5f);
}

float ncz_gpu_tier_float(float low_value, float high_value) {
    const ncz_gpu_tier_t *t = ncz_gpu_tier_current();
    float k = t->scalar;
    float v = low_value + k * (high_value - low_value);
    if (v < low_value)  v = low_value;
    if (v > high_value) v = high_value;
    return v;
}

float ncz_gpu_tier_scalar_for(ncz_tier_e t) {
    switch (t) {
        case NCZ_TIER_LOW:    return 0.0f;
        case NCZ_TIER_MEDIUM: return 0.33f;
        case NCZ_TIER_HIGH:   return 0.66f;
        case NCZ_TIER_ULTRA:  return 1.0f;
        default:              return 0.0f;
    }
}
