/* SPDX-License-Identifier: Apache-2.0 */
/* magmasimplex_goo_cfg.c - settings and the seeded randomizer (see header). */
#include "magmasimplex_goo_cfg.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Simulation seconds per wall second at speed 1. Calibrated against the
 * operator-supplied real-time recording of three lamps (docs/
 * MAGMASIMPLEX-60S.md, "Real lamp vs simulation"): moving free masses there
 * rise at about 0.02 globe heights/s (median) and 0.05 (p90); the simulation
 * (tests/magmasimplex_calib.c) gives 0.0168 and 0.034 heights per simulated
 * second, so 1.3 puts it at 0.022 and 0.044. */
#define TIME_SCALE 1.30f
float ms_goo_time_scale(void) { return TIME_SCALE; }

static const char *VIEWS[] = {"inside", "glass", "show", "field", "butterfly"};
static const char *WORLDS[] = {"tank", "dish", "radial", "drift"};
static const char *MATS[] = {"milky", "translucent", "jelly"};
static const char *ERAS[] = {"any", "1960s", "1970s", "1980s", "1990s", "modern"};
const char *ms_goo_view_name(int v) { return v < 0 ? "auto" : (v <= 4 ? VIEWS[v] : "?"); }
static const char *MASKS[] = {"off", "soft", "dish", "disc"};
const char *ms_goo_mask_name(int m) { return (m >= 0 && m <= 3) ? MASKS[m] : "?"; }
static const char *OVERLAYS[] = {"off", "streaks", "neon", "both"};
const char *ms_goo_overlay_name(int m) { return (m >= 0 && m <= 3) ? OVERLAYS[m] : "?"; }
static const char *AUDIOS[] = {"off", "system", "mic"};
const char *ms_goo_audio_name(int a) { return (a >= 0 && a <= 2) ? AUDIOS[a] : "?"; }
static const char *DENDRIES[] = {"off", "ink", "veins"};
const char *ms_goo_dendrites_name(int m) { return (m >= 0 && m <= 2) ? DENDRIES[m] : "?"; }

float ms_goo_mask_radius(int mask, float size) {
    if (mask != MS_MASK_DISH && mask != MS_MASK_DISC) return 0.f;
    if (!(size > 0.f)) return MS_MASK_SIZE_DEFAULT; /* NaN or <= 0: the style's default */
    if (size < MS_MASK_SIZE_MIN) size = MS_MASK_SIZE_MIN;
    if (size > MS_MASK_SIZE_MAX) size = MS_MASK_SIZE_MAX;
    return size;
}
const char *ms_goo_world_name(int w) { return (w >= 0 && w <= 3) ? WORLDS[w] : "?"; }
const char *ms_goo_material_name(int m) { return (m >= 0 && m <= 2) ? MATS[m] : "?"; }
const char *ms_goo_era_name(int e) { return (e >= 0 && e <= 5) ? ERAS[e] : "?"; }

void ms_goo_cfg_unset(MsGooConfig *c) {
    c->randomize = 1;
    c->view = c->world = c->palette = c->era = c->material = c->glitter = MS_UNSET_I;
    c->startup = c->cap = c->kaleidoscope = c->projector_mask = c->overlay = c->poster = MS_UNSET_I;
    c->tempo = c->audio = MS_UNSET_I;
    c->dendrites = MS_UNSET_I;
    c->texture = c->viscosity = c->wax = c->heat = c->speed = c->motes = c->glass = c->depth = c->drift = c->chapter =
        c->glitter_amount = c->projector_mask_size = c->dendrite_strength = NAN;
    c->dynamics = NAN;
    c->quality = 1;
}

/* One independent stream per setting: fixing one setting never reshuffles
 * the others for the same seed. */
static uint32_t stream(uint32_t seed, uint32_t k) {
    uint32_t h = seed * 0x9E3779B1u ^ (k + 1u) * 0x85EBCA77u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h ? h : 0x1234567u;
}
static float u01(uint32_t *s) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5;
    return (float)(*s & 0xffffffu) / 16777216.f;
}
static float urange(uint32_t seed, uint32_t k, float a, float b) {
    uint32_t s = stream(seed, k);
    return a + (b - a) * u01(&s);
}
static int pick(uint32_t seed, uint32_t k, const float *w, int n) {
    uint32_t s = stream(seed, k);
    float r = u01(&s), sum = 0;
    for (int i = 0; i < n; i++) sum += w[i];
    r *= sum;
    for (int i = 0; i < n; i++) {
        if (r < w[i]) return i;
        r -= w[i];
    }
    return n - 1;
}

static int era_of(const MsPaletteDef *d) {
    for (int e = 1; e <= 5; e++)
        if (!strcmp(d->era, ERAS[e])) return e;
    return MS_ERA_MODERN;
}

/* Palette draw: from the era's entries, else a mix of the whole table and
 * the generators; every draw must be readable. */
static int draw_palette(uint32_t seed, int era) {
    uint32_t s = stream(seed, 100);
    int n = ms_palette_count();
    /* One roll for a generated pair (about 15 %), then the table. */
    float r = u01(&s);
    int any = era == MS_ERA_ANY || era == MS_UNSET_I;
    if ((any || era == MS_ERA_1960S || era == MS_ERA_1970S) && r < 0.10f) return MS_PAL_GEN_VINTAGE;
    if ((any || era == MS_ERA_MODERN) && r >= 0.10f && r < 0.15f) return MS_PAL_GEN_NEON;
    for (int tries = 0; tries < 400; tries++) {
        int i = (int)(u01(&s) * (float)n) % n;
        const MsPaletteDef *d = ms_palette_def(i);
        /* Dye sets of the Flowerchild light show are not lamp pairings. */
        if (!strcmp(d->source, "light-show")) continue;
        if (!any && era_of(d) != era) continue;
        /* Weight: the sixties and the photos twice as likely as the rest. */
        if (era_of(d) != MS_ERA_1960S && strcmp(d->source, "photos") && u01(&s) < 0.5f) continue;
        MsPalette p;
        ms_palette_get(i, &p);
        if (ms_palette_readable(&p)) return i;
    }
    return 0;
}

#define FSET(x) (!isnan(x))
void ms_goo_cfg_resolve(MsGooConfig *c, uint32_t seed) {
    int R = c->randomize;
    if (c->view == MS_UNSET_I) c->view = MS_VIEW_AUTO;
    if (c->world == MS_UNSET_I) {
        static const float w[] = {0.5f, 0.15f, 0.15f, 0.2f};
        c->world = R ? pick(seed, 1, w, 4) : MS_WORLD_TANK;
    }
    if (c->era == MS_UNSET_I) c->era = MS_ERA_ANY;
    if (c->palette == MS_UNSET_I) {
        c->palette = R ? draw_palette(seed, c->era) : ms_palette_find("era-60s-yellow-orange");
        if (c->palette == -1) c->palette = 0;
    }
    if (c->material == MS_UNSET_I) {
        /* jelly (the glossy candy look) is never drawn: it is opt-in, by
         * material=jelly or the candy-jelly scene (operator, 2026-10-04). */
        static const float w[] = {0.70f, 0.30f, 0.0f};
        c->material = R ? pick(seed, 2, w, 3) : MS_MAT_MILKY;
    }
    if (c->glitter == MS_UNSET_I) c->glitter = -1;
    if (c->startup == MS_UNSET_I) c->startup = 0; /* explicit only: it is a scene of its own */
    if (c->cap == MS_UNSET_I) c->cap = R ? (urange(seed, 3, 0, 1) < 0.75f) : 1;
    if (c->kaleidoscope == MS_UNSET_I) {
        static const float w[] = {0.6f, 0.15f, 0.15f, 0.1f};
        static const int k[] = {0, 4, 6, 8};
        c->kaleidoscope = R ? k[pick(seed, 4, w, 4)] : 0;
    }
    if (!FSET(c->texture)) c->texture = R ? urange(seed, 5, 0.6f, 1.4f) : 1.f;
    if (!FSET(c->viscosity)) c->viscosity = R ? urange(seed, 6, 0.8f, 1.1f) : 1.f;
    if (!FSET(c->wax)) c->wax = R ? urange(seed, 7, 0.85f, 1.1f) : 1.f;
    if (!FSET(c->heat)) c->heat = R ? urange(seed, 8, 0.85f, 1.15f) : 1.f;
    if (!FSET(c->speed)) c->speed = 1.f; /* real-lamp pace unless asked */
    if (!FSET(c->motes)) c->motes = R ? urange(seed, 9, 0.5f, 1.5f) : 1.f;
    if (!FSET(c->glass)) c->glass = R ? urange(seed, 10, 0.5f, 1.5f) : 1.f;
    if (!FSET(c->depth)) c->depth = R ? urange(seed, 11, 0.7f, 1.3f) : 1.f;
    if (!FSET(c->drift)) c->drift = R ? urange(seed, 12, 0.5f, 1.5f) : 1.f;
    if (!FSET(c->chapter)) c->chapter = 1.f;
    if (c->chapter < 0.1f) c->chapter = 0.1f;
    if (!FSET(c->glitter_amount)) c->glitter_amount = 1.f;
    /* Not drawn: the projector mask is a scene choice (operator, 2026-10-05). */
    if (c->projector_mask == MS_UNSET_I) c->projector_mask = MS_MASK_SOFT;
    if (c->overlay == MS_UNSET_I) c->overlay = MS_OVERLAY_OFF; /* scenes only */
    if (c->poster == MS_UNSET_I) c->poster = 0;
    /* The beat layer is opt-in only: never drawn, so every existing look is
     * unchanged unless a scene or the user raises dynamics. audio defaults to
     * off for privacy (the microphone is never listened to by default) and is
     * moot when nothing is driven. */
    if (!FSET(c->dynamics)) c->dynamics = 0.f;
    if (c->dynamics < 0.f) c->dynamics = 0.f;
    if (c->dynamics > 1.f) c->dynamics = 1.f;
    if (c->tempo == MS_UNSET_I) c->tempo = 0;
    if (c->tempo != 0) {
        if (c->tempo < 60) c->tempo = 60;
        if (c->tempo > 180) c->tempo = 180;
    }
    if (c->audio == MS_UNSET_I) c->audio = MS_AUDIO_OFF;
    if (c->audio < MS_AUDIO_OFF || c->audio > MS_AUDIO_MIC) c->audio = MS_AUDIO_OFF;
    if (c->dynamics <= 0.f) c->audio = MS_AUDIO_OFF; /* no capture without drives */
    /* Dendrites and the projector-mask size are scene choices: not drawn, so
     * a random launch keeps the existing looks. */
    if (c->dendrites == MS_UNSET_I) c->dendrites = MS_DEND_OFF;
    if (!FSET(c->dendrite_strength)) c->dendrite_strength = 1.f;
    if (c->dendrite_strength < 0.f) c->dendrite_strength = 0.f;
    if (c->dendrite_strength > 1.f) c->dendrite_strength = 1.f;
    if (!FSET(c->projector_mask_size)) c->projector_mask_size = 0.f; /* 0 = style default */
}

void ms_goo_cfg_palette(const MsGooConfig *c, uint32_t seed, MsPalette *out) {
    if (c->palette == MS_PAL_GEN_VINTAGE || c->palette == MS_PAL_GEN_NEON) {
        uint32_t s = stream(seed, 101);
        ms_palette_generate(&s, c->palette == MS_PAL_GEN_NEON, out);
    } else {
        ms_palette_get(c->palette, out);
    }
    if (c->glitter >= 0) out->glitter = c->glitter;
}

void ms_goo_cfg_describe(const MsGooConfig *c, char *out, size_t n) {
    const MsPaletteDef *d = c->palette >= 0 ? ms_palette_def(c->palette) : NULL;
    snprintf(out, n,
             "view=%s world=%s palette=%s palette-era=%s material=%s glitter=%d startup=%d cap=%d kaleidoscope=%d "
             "texture=%.2f viscosity=%.2f wax=%.2f heat=%.2f speed=%.2f motes=%.2f glass=%.2f depth=%.2f drift=%.2f "
             "chapter=%.2f projector-mask=%s projector-mask-size=%.2f overlay=%s poster=%d "
             "dendrites=%s dendrite-strength=%.2f randomize=%s "
             "dynamics=%.2f tempo=%d audio=%s",
             ms_goo_view_name(c->view), ms_goo_world_name(c->world),
             d ? d->id : (c->palette == MS_PAL_GEN_NEON ? "generated-neon" : "generated-vintage"), ms_goo_era_name(c->era),
             ms_goo_material_name(c->material), c->glitter, c->startup, c->cap, c->kaleidoscope, c->texture,
             c->viscosity, c->wax, c->heat, c->speed, c->motes, c->glass, c->depth, c->drift, c->chapter,
             ms_goo_mask_name(c->projector_mask), c->projector_mask_size, ms_goo_overlay_name(c->overlay), c->poster,
             ms_goo_dendrites_name(c->dendrites), c->dendrite_strength, c->randomize ? "auto" : "off",
             c->dynamics, c->tempo, ms_goo_audio_name(c->audio));
}
